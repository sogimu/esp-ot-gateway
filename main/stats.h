#pragma once

#include <stdint.h>

/* Запись замера модуляции (только >0%) */
void stats_record_modulation(float mod);

/* Переходы горелки */
void stats_flame_on(void);
void stats_flame_off(void);

/* Перцентиль (p=1..99) */
float stats_percentile(float p);

/* Геттеры */
int   stats_sample_count(void);
int   stats_cycle_count(void);
float stats_median_burn_sec(void);
float stats_median_pause_sec(void);
float stats_avg_burn_sec(void);
float stats_avg_pause_sec(void);
float stats_burner_hours(void);

/* JSON для /api/stats */
const char *stats_to_json(void);
