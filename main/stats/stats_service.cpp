#include "stats/stats_service.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

// --- GasFlowEstimator ---

GasFlowEstimator::GasFlowEstimator()
    : flow_max_(24.0f / 9.5f)
    , k_calib_(1.0f)
    , integral_m3_(0.0f)
    , kalman_mod_(0.0f, 0.1f, 1.0f)
    , kalman_ret_(0.0f, 0.05f, 0.3f)
    , latest_flow_(0.0f)
    , mod_filt_(0.0f)
    , t_ret_filt_(0.0f)
    , ring_idx_(0), ring_count_(0)
    , ema_1h_(0.0f), ema_3h_(0.0f), ema_12h_(0.0f), ema_24h_(0.0f), ema_7d_(0.0f)
    , ema_start_us_(0)
{
    std::memset(ring_, 0, sizeof(ring_));
}

void GasFlowEstimator::set_params(float p_max_kw, float gas_cal)
{
    flow_max_ = p_max_kw / gas_cal;
}

void GasFlowEstimator::set_k_calib(float v)
{
    k_calib_ = v;
}

float GasFlowEstimator::get_k_calib() const
{
    return k_calib_;
}

float GasFlowEstimator::eta_corr(float t_ret)
{
    if (t_ret <= 30.0f) return 1.076f;
    if (t_ret >= 60.0f) return 0.976f;
    return 1.076f - 0.003333f * (t_ret - 30.0f);
}

void GasFlowEstimator::update(float mod_raw, float t_ret_raw, uint32_t dt_ms)
{
    // Kalman filtering
    mod_filt_ = kalman_mod_.update(mod_raw);
    t_ret_filt_ = kalman_ret_.update(t_ret_raw);

    // Instantaneous flow (m³/h)
    latest_flow_ = k_calib_ * (mod_filt_ / 100.0f) * flow_max_ * eta_corr(t_ret_filt_);
    if (latest_flow_ < 0.0f) latest_flow_ = 0.0f;

    // Integration (dt_ms -> hours)
    float dt_hour = (float)dt_ms / 3600000.0f;
    integral_m3_ += latest_flow_ * dt_hour;

    // Ring buffer fill
    ring_[ring_idx_].flow = latest_flow_;
    ring_idx_ = (ring_idx_ + 1) % GAS_RING_SIZE;
    if (ring_count_ < GAS_RING_SIZE) ring_count_++;

    // EMA update
    float alpha = 1.0f - expf(-dt_hour * 3600.0f / 3600.0f); // 1h time constant
    if (ema_start_us_ == 0) {
        ema_1h_ = latest_flow_;
        ema_3h_ = latest_flow_;
        ema_12h_ = latest_flow_;
        ema_24h_ = latest_flow_;
        ema_7d_ = latest_flow_;
        ema_start_us_ = esp_timer_get_time();
    } else {
        ema_1h_ += alpha * (latest_flow_ - ema_1h_);
        float a3 = 1.0f - expf(-dt_hour * 3600.0f / (3.0f * 3600.0f));
        ema_3h_ += a3 * (latest_flow_ - ema_3h_);
        float a12 = 1.0f - expf(-dt_hour * 3600.0f / (12.0f * 3600.0f));
        ema_12h_ += a12 * (latest_flow_ - ema_12h_);
        float a24 = 1.0f - expf(-dt_hour * 3600.0f / (24.0f * 3600.0f));
        ema_24h_ += a24 * (latest_flow_ - ema_24h_);
        float a7 = 1.0f - expf(-dt_hour * 3600.0f / (7.0f * 24.0f * 3600.0f));
        ema_7d_ += a7 * (latest_flow_ - ema_7d_);
    }
}

void GasFlowEstimator::reset_integral()
{
    integral_m3_ = 0.0f;
    std::memset(ring_, 0, sizeof(ring_));
    ring_idx_ = 0;
    ring_count_ = 0;
    ema_1h_ = 0.0f;
    ema_3h_ = 0.0f;
    ema_12h_ = 0.0f;
    ema_24h_ = 0.0f;
    ema_7d_ = 0.0f;
    ema_start_us_ = 0;
}

void GasFlowEstimator::push_to_model(Model& model)
{
    GasData d;
    d.instant_flow   = latest_flow_;
    d.integral_m3    = integral_m3_;
    d.avg_1h         = ema_1h_;
    d.avg_3h         = ema_3h_;
    d.avg_12h        = ema_12h_;
    d.avg_24h        = ema_24h_;
    d.avg_7d         = ema_7d_;

    // Compute 1h sliding average from ring buffer (more accurate than EMA)
    if (ring_count_ > 0) {
        // How many samples fit in 1h (3600s / 10s = 360)
        int want = ring_count_ < 360 ? ring_count_ : 360;
        float sum = 0;
        int idx = ring_idx_;
        for (int i = 0; i < want; i++) {
            idx = (idx - 1 + GAS_RING_SIZE) % GAS_RING_SIZE;
            sum += ring_[idx].flow;
        }
        d.avg_1h = sum / (float)want;
    }

    d.mod_filtered   = mod_filt_;
    d.t_ret_filtered = t_ret_filt_;

    model.set_gas_data(d);
}

// --- StatsService ---

StatsService::StatsService(Model& model, OpenthermEndpoint& ot)
    : model_(model), ot_(ot)
    , samples_(0)
    , cycle_idx_(0), cycle_total_(0)
    , burner_sec_(0), cycle_cnt_(0)
    , flame_on_ms_(0), flame_off_ms_(0)
    , gas_()
{
    std::memset(hist_, 0, sizeof(hist_));
    std::memset(burn_dur_, 0, sizeof(burn_dur_));
    std::memset(pause_dur_, 0, sizeof(pause_dur_));
}

void StatsService::start()
{
    if (started_) return;
load_nvs_meter();

    load_nvs();
    ot_.subscribe(this);
    started_ = true;

    // Handle initial flame state: if boiler already burning, record timestamp
    if (flame_on_ms_ == 0 && model_.is_flame_on()) {
        flame_on_ms_ = (uint32_t)(esp_timer_get_time() / 1000);
    }

    // Start periodic tick every 30 seconds
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &StatsService::tick_callback_static;
    timer_args.arg = this;
    timer_args.name = "stats_tick";
    esp_timer_create(&timer_args, &tick_timer_);
    esp_timer_start_periodic(tick_timer_, 30000000);

    push_stats();
}

void StatsService::stop()
{
    if (!started_) return;

    if (tick_timer_) {
        esp_timer_stop(tick_timer_);
        esp_timer_delete(tick_timer_);
        tick_timer_ = nullptr;
    }

    // Flush any remaining burner time
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&stats_mux_);
    if (flame_on_ms_ > 0 && now > flame_on_ms_) {
        burner_sec_ += (now - flame_on_ms_) / 1000;
        flame_on_ms_ = 0;
    }
    portEXIT_CRITICAL(&stats_mux_);

    save_nvs();
    ot_.unsubscribe(this);
    started_ = false;
}

void StatsService::on_modulation(float pct)
{
    if (pct < 0.0f) return;
    int bin = (int)(pct * 10.0f + 0.5f) - 1;
    if (bin < 0) bin = 0;
    if (bin >= HIST_BINS) bin = HIST_BINS - 1;
    hist_[bin]++;
    samples_++;

    latest_mod_raw_ = pct;
    try_gas_estimate();
}

void StatsService::on_return_temp(float value)
{
    latest_ret_raw_ = value;
    try_gas_estimate();
}

void StatsService::try_gas_estimate()
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_gas_ms_ < 10000) return;
    if (last_gas_ms_ == 0) {
        last_gas_ms_ = now;
        return;
    }

    uint32_t dt_ms = now - last_gas_ms_;
    last_gas_ms_ = now;

    // Sync calibration coefficient from model
    gas_.set_k_calib(model_.get_k_calib());
    gas_.set_params(model_.get_p_max(), model_.get_gas_calorific());

    gas_.update(latest_mod_raw_, latest_ret_raw_, dt_ms);
    gas_.push_to_model(model_);
}

void StatsService::on_status_changed(bool fault, bool flame,
                                      bool ch_active, bool dhw_active)
{
    (void)fault; (void)ch_active; (void)dhw_active;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (flame) {
        if (flame_off_ms_ > 0) {
            uint32_t pause = (now - flame_off_ms_) / 1000;
            if (pause > 0 && pause <= 65535) {
                pause_dur_[cycle_idx_] = (uint16_t)pause;
            }
        }
        portENTER_CRITICAL(&stats_mux_);
        flame_on_ms_ = now;
        portEXIT_CRITICAL(&stats_mux_);
    } else {
        portENTER_CRITICAL(&stats_mux_);
        if (flame_on_ms_ > 0) {
            uint32_t dur = (now - flame_on_ms_) / 1000;
            if (dur > 0) {
                burner_sec_ += dur;
                uint16_t dur_cap = (dur > 65535) ? 65535 : (uint16_t)dur;
                burn_dur_[cycle_idx_] = dur_cap;
                cycle_idx_ = (cycle_idx_ + 1) % CYCLE_RING;
                if (cycle_total_ < CYCLE_RING) cycle_total_++;
                cycle_cnt_++;
            }
            flame_on_ms_ = 0;
        }
        portEXIT_CRITICAL(&stats_mux_);
        flame_off_ms_ = now;

        save_nvs();
    }

    push_stats();
}

static float median(uint16_t* src, int n)
{
    if (n <= 0 || n > CYCLE_RING) return 0.0f;
    uint16_t tmp[CYCLE_RING];
    memcpy(tmp, src, (size_t)n * sizeof(uint16_t));

    int lo = 0, hi = n - 1, mid = n / 2;
    while (lo < hi) {
        uint16_t pivot = tmp[(lo + hi) / 2];
        int i = lo, j = hi;
        while (i <= j) {
            while (tmp[i] < pivot) i++;
            while (tmp[j] > pivot) j--;
            if (i <= j) {
                uint16_t t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
                i++; j--;
            }
        }
        if (mid <= j) hi = j;
        else if (mid >= i) lo = i;
        else break;
    }

    float m;
    if (n % 2 == 0) {
        uint16_t a = tmp[mid], b = 0;
        for (int k = 0; k < n; k++) {
            if (k != mid && tmp[k] >= a) { b = tmp[k]; break; }
        }
        m = (float)(a + b) / 2.0f;
    } else {
        m = (float)tmp[mid];
    }
    return m;
}

void StatsService::push_stats()
{
    StatsData d;
    d.sample_count = (int)samples_;
    d.cycle_count  = cycle_total_;
    d.median_burn  = median(burn_dur_, cycle_total_);
    d.median_pause = median(pause_dur_, cycle_total_);

    if (cycle_total_ > 0) {
        uint32_t sum = 0;
        for (int i = 0; i < cycle_total_; i++) sum += burn_dur_[i];
        d.avg_burn = (float)sum / (float)cycle_total_;

        sum = 0;
        for (int i = 0; i < cycle_total_; i++) sum += pause_dur_[i];
        d.avg_pause = (float)sum / (float)cycle_total_;
    }

    // Include ongoing burn time
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t total_sec;

    portENTER_CRITICAL(&stats_mux_);
    total_sec = burner_sec_;
    if (flame_on_ms_ > 0 && now > flame_on_ms_) {
        total_sec += (now - flame_on_ms_) / 1000;
    }
    portEXIT_CRITICAL(&stats_mux_);

    d.burner_hours = (float)total_sec / 3600.0f;

    if (samples_ > 0) {
        uint32_t targets[7] = {
            (uint32_t)((double)samples_ * 0.01),
            (uint32_t)((double)samples_ * 0.10),
            (uint32_t)((double)samples_ * 0.25),
            (uint32_t)((double)samples_ * 0.50),
            (uint32_t)((double)samples_ * 0.75),
            (uint32_t)((double)samples_ * 0.90),
            (uint32_t)((double)samples_ * 0.99)
        };
        float* out[7] = {&d.p1, &d.p10, &d.p25, &d.p50, &d.p75, &d.p90, &d.p99};

        uint32_t cum = 0;
        int ti = 0;
        for (int i = 0; i < HIST_BINS && ti < 7; i++) {
            cum += hist_[i];
            while (ti < 7 && cum > targets[ti]) {
                *out[ti] = (float)(i + 1) / 10.0f;
                ti++;
            }
        }
        for (; ti < 7; ti++)
            *out[ti] = 100.0f;
    }

    model_.set_stats(d);
}

// --- NVS persistence for meter corrections ---

struct __attribute__((packed)) NvsCorrLogEntry {
    uint32_t timestamp;
    float actual_reading;
    float estimated_total;
    float difference;
    float prev_k_calib;
    float new_k_calib;
};

struct __attribute__((packed)) NvsMeterBlob {
    float base_reading;
    float last_correction_actual;
    float integral_at_last_correction;
    int32_t corrections_head;
    int32_t corrections_count;
    NvsCorrLogEntry corrections[CORRECTION_LOG_SIZE];
};

void StatsService::load_nvs_meter()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("meter", NVS_READONLY, &h);
    if (err != ESP_OK) return;

    NvsMeterBlob* blob = new NvsMeterBlob;
    size_t sz = sizeof(*blob);
    if (nvs_get_blob(h, "data", blob, &sz) == ESP_OK) {
        model_.set_gas_meter_base(blob->base_reading);
        model_.set_last_correction_refs(blob->last_correction_actual,
                                         blob->integral_at_last_correction);
        int n = blob->corrections_count;
        if (n > CORRECTION_LOG_SIZE) n = CORRECTION_LOG_SIZE;
        for (int i = 0; i < n; i++) {
            CorrectionEntry e;
            e.timestamp       = blob->corrections[i].timestamp;
            e.actual_reading  = blob->corrections[i].actual_reading;
            e.estimated_total = blob->corrections[i].estimated_total;
            e.difference      = blob->corrections[i].difference;
            e.prev_k_calib    = blob->corrections[i].prev_k_calib;
            e.new_k_calib     = blob->corrections[i].new_k_calib;
            model_.add_correction(e);
        }
    }
    delete blob;
    nvs_close(h);
}

void StatsService::save_nvs_meter()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("meter", NVS_READWRITE, &h);
    if (err != ESP_OK) return;

    NvsMeterBlob* blob = new NvsMeterBlob;
    memset(blob, 0, sizeof(*blob));
    blob->base_reading      = model_.get_gas_meter_base();
    blob->last_correction_actual     = model_.get_last_correction_actual();
    blob->integral_at_last_correction = model_.get_integral_at_last_correction();
    blob->corrections_count = model_.get_correction_count();
    blob->corrections_head  = 0;

    int total = model_.get_correction_count();
    for (int i = 0; i < total && i < CORRECTION_LOG_SIZE; i++) {
        CorrectionEntry src;
        model_.get_correction_by_index(i, src);
        blob->corrections[i].timestamp       = src.timestamp;
        blob->corrections[i].actual_reading  = src.actual_reading;
        blob->corrections[i].estimated_total = src.estimated_total;
        blob->corrections[i].difference      = src.difference;
        blob->corrections[i].prev_k_calib    = src.prev_k_calib;
        blob->corrections[i].new_k_calib     = src.new_k_calib;
    }

    nvs_set_blob(h, "data", blob, sizeof(*blob));
    nvs_commit(h);
    nvs_close(h);
    delete blob;
}

// --- NVS blob structures ---

struct __attribute__((packed)) NvsHistBlob {
    uint32_t samples;
    uint16_t hist[HIST_BINS];
};

struct __attribute__((packed)) NvsCycleBlob {
    uint32_t burner_sec;
    uint32_t cycle_cnt;
    int32_t cycle_idx;
    int32_t cycle_total;
    uint16_t burn_dur[CYCLE_RING];
    uint16_t pause_dur[CYCLE_RING];
};

struct __attribute__((packed)) NvsGasEmaBlob {
    float ema_1h;
    float ema_3h;
    float ema_12h;
    float ema_24h;
    float ema_7d;
    uint64_t ema_start_us;
};

struct __attribute__((packed)) NvsCalibBlob {
    float k_calib;
    float p_max;
    float gas_calorific;
};

// --- NVS I/O ---

void StatsService::load_nvs()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("stats", NVS_READONLY, &h);
    if (err != ESP_OK) return;

    uint32_t u32val = 0;
    if (nvs_get_u32(h, "burn_sec", &u32val) == ESP_OK) {
        burner_sec_ = u32val;
    }

    float fval = 0;
    size_t sz = sizeof(fval);
    if (nvs_get_blob(h, "integ_m3", &fval, &sz) == ESP_OK) {
        gas_.set_integral_m3(fval);
    }

    NvsGasEmaBlob* ema = new NvsGasEmaBlob;
    sz = sizeof(*ema);
    if (nvs_get_blob(h, "gas_ema", ema, &sz) == ESP_OK) {
        gas_.set_ema_1h(ema->ema_1h);
        gas_.set_ema_3h(ema->ema_3h);
        gas_.set_ema_12h(ema->ema_12h);
        gas_.set_ema_24h(ema->ema_24h);
        gas_.set_ema_7d(ema->ema_7d);
        gas_.set_ema_start_us(ema->ema_start_us);
    }
    delete ema;

    NvsHistBlob* hist = new NvsHistBlob;
    sz = sizeof(*hist);
    if (nvs_get_blob(h, "hist", hist, &sz) == ESP_OK) {
        samples_ = hist->samples;
        memcpy(hist_, hist->hist, sizeof(hist_));
    }
    delete hist;

    NvsCycleBlob* cycles = new NvsCycleBlob;
    sz = sizeof(*cycles);
    if (nvs_get_blob(h, "cycles", cycles, &sz) == ESP_OK) {
        burner_sec_ = cycles->burner_sec;
        cycle_cnt_ = cycles->cycle_cnt;
        cycle_idx_ = cycles->cycle_idx;
        cycle_total_ = cycles->cycle_total;
        memcpy(burn_dur_, cycles->burn_dur, sizeof(burn_dur_));
        memcpy(pause_dur_, cycles->pause_dur, sizeof(pause_dur_));
    }
    delete cycles;

    NvsCalibBlob* calib = new NvsCalibBlob;
    sz = sizeof(*calib);
    if (nvs_get_blob(h, "calib", calib, &sz) == ESP_OK) {
        model_.set_k_calib(calib->k_calib);
        model_.set_p_max(calib->p_max);
        model_.set_gas_calorific(calib->gas_calorific);
    }
    delete calib;

    nvs_close(h);
}

void StatsService::save_nvs()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("stats", NVS_READWRITE, &h);
    if (err != ESP_OK) return;

    nvs_set_u32(h, "burn_sec", burner_sec_);

    float fval = gas_.get_integral_m3();
    nvs_set_blob(h, "integ_m3", &fval, sizeof(fval));

    NvsGasEmaBlob* ema = new NvsGasEmaBlob;
    ema->ema_1h = gas_.get_ema_1h();
    ema->ema_3h = gas_.get_ema_3h();
    ema->ema_12h = gas_.get_ema_12h();
    ema->ema_24h = gas_.get_ema_24h();
    ema->ema_7d = gas_.get_ema_7d();
    ema->ema_start_us = gas_.get_ema_start_us();
    nvs_set_blob(h, "gas_ema", ema, sizeof(*ema));
    delete ema;

    NvsHistBlob* hist = new NvsHistBlob;
    hist->samples = samples_;
    memcpy(hist->hist, hist_, sizeof(hist_));
    nvs_set_blob(h, "hist", hist, sizeof(*hist));
    delete hist;

    NvsCycleBlob* cycles = new NvsCycleBlob;
    cycles->burner_sec = burner_sec_;
    cycles->cycle_cnt = cycle_cnt_;
    cycles->cycle_idx = cycle_idx_;
    cycles->cycle_total = cycle_total_;
    memcpy(cycles->burn_dur, burn_dur_, sizeof(burn_dur_));
    memcpy(cycles->pause_dur, pause_dur_, sizeof(pause_dur_));
    nvs_set_blob(h, "cycles", cycles, sizeof(*cycles));
    delete cycles;

    NvsCalibBlob* calib = new NvsCalibBlob;
    calib->k_calib = model_.get_k_calib();
    calib->p_max = model_.get_p_max();
    calib->gas_calorific = model_.get_gas_calorific();
    nvs_set_blob(h, "calib", calib, sizeof(*calib));
    delete calib;

    nvs_commit(h);
    nvs_close(h);

    last_nvs_save_sec_ = (uint32_t)(esp_timer_get_time() / 1000000);
}

// --- Periodic tick ---

void StatsService::tick_callback_static(void* arg)
{
    static_cast<StatsService*>(arg)->periodic_tick();
}

void StatsService::periodic_tick()
{
    tick_count_++;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    // Accumulate burner time while flame is on
    portENTER_CRITICAL(&stats_mux_);
    if (flame_on_ms_ > 0 && now > flame_on_ms_) {
        uint32_t dur = (now - flame_on_ms_) / 1000;
        if (dur > 0) {
            burner_sec_ += dur;
            flame_on_ms_ = now;
        }
    }
    portEXIT_CRITICAL(&stats_mux_);

    // Re-push stats to model so ongoing burner time is reflected
    push_stats();

    // Save to NVS every 10 minutes (20 ticks * 30s = 600s = 10min)
    // Also save if enough time passed since last save
    uint32_t now_sec = now / 1000;
    if (now_sec - last_nvs_save_sec_ >= 600) {
        save_nvs();
    }
}

// --- Reset methods ---

void StatsService::reset_modulation_stats()
{
    std::memset(hist_, 0, sizeof(hist_));
    samples_ = 0;
    save_nvs();
    push_stats();
}

void StatsService::reset_cycle_stats()
{
    std::memset(burn_dur_, 0, sizeof(burn_dur_));
    std::memset(pause_dur_, 0, sizeof(pause_dur_));
    cycle_idx_ = 0;
    cycle_total_ = 0;
    cycle_cnt_ = 0;
    portENTER_CRITICAL(&stats_mux_);
    burner_sec_ = 0;
    flame_on_ms_ = 0;
    flame_off_ms_ = 0;
    portEXIT_CRITICAL(&stats_mux_);
    save_nvs();
    push_stats();
}

void StatsService::reset_gas_stats()
{
    gas_.reset_integral();
    save_nvs();
    push_stats();
}
}