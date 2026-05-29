#include "stats/stats_service.h"

#include <cstdio>
#include <cstring>
#include "esp_timer.h"

StatsService::StatsService(Model& model, OpenthermEndpoint& ot)
    : model_(model), ot_(ot)
    , samples_(0)
    , cycle_idx_(0), cycle_total_(0)
    , burner_sec_(0), cycle_cnt_(0)
    , flame_on_ms_(0), flame_off_ms_(0)
{
    std::memset(hist_, 0, sizeof(hist_));
    std::memset(burn_dur_, 0, sizeof(burn_dur_));
    std::memset(pause_dur_, 0, sizeof(pause_dur_));
}

void StatsService::start()
{
    if (started_) return;
    ot_.subscribe(this);
    started_ = true;
}

void StatsService::stop()
{
    if (!started_) return;
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
        flame_on_ms_ = now;
    } else {
        if (flame_on_ms_ > 0) {
            uint32_t dur = (now - flame_on_ms_) / 1000;
            if (dur > 0 && dur <= 65535) {
                burn_dur_[cycle_idx_] = (uint16_t)dur;
                cycle_idx_ = (cycle_idx_ + 1) % CYCLE_RING;
                if (cycle_total_ < CYCLE_RING) cycle_total_++;
                burner_sec_ += dur;
                cycle_cnt_++;
            }
        }
        flame_off_ms_ = now;
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

    d.burner_hours = (float)burner_sec_ / 3600.0f;

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