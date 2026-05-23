#include "stats.h"

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"

/* ── Гистограмма модуляции (0.1-100.0%, по 0.1% на bin) ── */
#define HIST_BINS 1000

static uint16_t s_hist[HIST_BINS];
static uint32_t s_samples;

/* ── Циклы горения ── */
#define CYCLE_RING 256

static uint16_t s_burn_dur[CYCLE_RING];
static uint16_t s_pause_dur[CYCLE_RING];
static int      s_cycle_idx;        /* следующий для записи */
static int      s_cycle_total;      /* всего циклов в буфере (≤ CYCLE_RING) */

static uint32_t s_burner_sec;       /* интеграл: секунд горения */
static uint32_t s_cycle_cnt;        /* интеграл: всего циклов */

static uint32_t s_flame_on_ms;      /* timestamp включения */
static uint32_t s_flame_off_ms;     /* timestamp выключения */

/* ── Запись модуляции ── */

void stats_record_modulation(float mod)
{
    if (mod < 0.0f) return;
    int bin = (int)(mod * 10.0f + 0.5f) - 1;
    if (bin < 0) bin = 0;
    if (bin >= HIST_BINS) bin = HIST_BINS - 1;
    s_hist[bin]++;
    s_samples++;
}

/* ── Переходы горелки ── */

void stats_flame_on(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_flame_off_ms > 0) {
        uint32_t pause = (now - s_flame_off_ms) / 1000;
        if (pause > 0 && pause <= 65535) {
            s_pause_dur[s_cycle_idx] = (uint16_t)pause;
        }
    }
    s_flame_on_ms = now;
}

void stats_flame_off(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (s_flame_on_ms > 0) {
        uint32_t dur = (now - s_flame_on_ms) / 1000;
        if (dur > 0 && dur <= 65535) {
            s_burn_dur[s_cycle_idx] = (uint16_t)dur;
            s_cycle_idx = (s_cycle_idx + 1) % CYCLE_RING;
            if (s_cycle_total < CYCLE_RING) s_cycle_total++;
            s_burner_sec += dur;
            s_cycle_cnt++;
        }
    }
    s_flame_off_ms = now;
}

/* ── Перцентиль из гистограммы ── */

float stats_percentile(float p)
{
    if (s_samples == 0) return 0.0f;
    uint32_t target = (uint32_t)((double)s_samples * (double)p / 100.0);
    if (target >= s_samples) target = s_samples - 1;
    uint32_t cum = 0;
    for (int i = 0; i < HIST_BINS; i++) {
        cum += s_hist[i];
        if (cum > target) return (float)(i + 1) / 10.0f;
    }
    return 100.0f;
}

/* ── Геттеры ── */

int stats_sample_count(void) { return (int)s_samples; }
int stats_cycle_count(void)  { return s_cycle_total; }

/* Медиана: используем статический буфер (без malloc) */
static float median(uint16_t *src, int n)
{
    if (n <= 0 || n > CYCLE_RING) return 0.0f;
    static uint16_t tmp[CYCLE_RING];   /* 512 байт, достаточно для всего кольца */
    memcpy(tmp, src, (size_t)n * sizeof(uint16_t));

    /* быстрая частичная сортировка для медианы */
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

float stats_median_burn_sec(void)  { return median(s_burn_dur, s_cycle_total); }
float stats_median_pause_sec(void) { return median(s_pause_dur, s_cycle_total); }

float stats_avg_burn_sec(void) {
    if (s_cycle_total == 0) return 0.0f;
    uint32_t sum = 0;
    for (int i = 0; i < s_cycle_total; i++) sum += s_burn_dur[i];
    return (float)sum / (float)s_cycle_total;
}

float stats_avg_pause_sec(void) {
    if (s_cycle_total == 0) return 0.0f;
    uint32_t sum = 0;
    for (int i = 0; i < s_cycle_total; i++) sum += s_pause_dur[i];
    return (float)sum / (float)s_cycle_total;
}

float stats_burner_hours(void) {
    return (float)s_burner_sec / 3600.0f;
}

/* ── JSON ── */

const char *stats_to_json(void)
{
    static char buf[2048];
    float p90 = stats_percentile(90);
    float p99 = stats_percentile(99);
    float p10 = stats_percentile(10);
    float p50 = stats_percentile(50);

    snprintf(buf, sizeof(buf),
        "{"
        "\"samples\":%d,"
        "\"p1\":%.1f,\"p10\":%.1f,\"p25\":%.1f,\"p50\":%.1f,"
        "\"p75\":%.1f,\"p90\":%.1f,\"p99\":%.1f,"
        "\"cycles\":%d,"
        "\"med_burn\":%.0f,\"med_pause\":%.0f,"
        "\"avg_burn\":%.0f,\"avg_pause\":%.0f,"
        "\"burner_h\":%.1f,"
        "\"p90_max\":%.1f,\"p10_p50\":%.1f,\"p99_p90\":%.1f"
        "}",
        (int)s_samples,
        (double)stats_percentile(1), (double)p10,
        (double)stats_percentile(25), (double)p50,
        (double)stats_percentile(75), (double)p90, (double)p99,
        s_cycle_total,
        (double)stats_median_burn_sec(), (double)stats_median_pause_sec(),
        (double)stats_avg_burn_sec(), (double)stats_avg_pause_sec(),
        (double)stats_burner_hours(),
        (double)p90, (double)(p10 / (p50 > 0 ? p50 : 1)),
        (double)(p99 - p90)
    );
    return buf;
}
