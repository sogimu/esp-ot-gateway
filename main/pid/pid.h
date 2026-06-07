#pragma once

#include <stdint.h>
#include <math.h>

typedef struct {
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
} PidConfig;

#define PID_CONFIG_DEFAULT() \
    { .kp = 2.0f, .ki = 0.01f, .kd = 0.0f, .out_min = 25.0f, .out_max = 75.0f }

typedef struct {
    float integral;
    float prev_temp;
    float d_filt;
} PidState;

#define PID_STATE_DEFAULT() \
    { .integral = 0, .prev_temp = 0, .d_filt = 0 }

static inline void pid_reset(PidState* s, float temp)
{
    s->integral = 0;
    s->prev_temp = temp;
    s->d_filt = 0;
}

static inline float pid_step(PidState* s, const PidConfig* cfg, float setpoint, float measured, int dt_sec)
{
    if (dt_sec <= 0) dt_sec = 1;
    float dt = (float)dt_sec;

    float error = setpoint - measured;

    float p = cfg->kp * error;

    float i_raw = s->integral + cfg->ki * error * dt;

    float d_raw = cfg->kd * (s->prev_temp - measured) / dt;
    s->d_filt = 0.9f * s->d_filt + 0.1f * d_raw;

    float output_raw = p + i_raw + s->d_filt;

    float output = output_raw;
    if (output < cfg->out_min) output = cfg->out_min;
    if (output > cfg->out_max) output = cfg->out_max;

    if (output_raw == output) {
        s->integral = i_raw;
    }

    s->prev_temp = measured;

    return output;
}