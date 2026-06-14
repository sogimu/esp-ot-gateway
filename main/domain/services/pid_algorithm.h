#pragma once

/// Pure PID algorithm — matches pid/pid.h implementation.
/// Zero dependencies — usable in host tests.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;
    float out_min;
    float out_max;
} PidAlgoCfg;

typedef struct {
    float integral;
    float prev_temp;
    float d_filt;
} PidAlgoState;

static inline void pid_init(PidAlgoState* s)
{
    s->integral = 0;
    s->prev_temp = 0;
    s->d_filt = 0;
}

static inline float pid_step(PidAlgoCfg* cfg, PidAlgoState* s, float setpoint, float measured, int dt_sec)
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
    bool at_min = false, at_max = false;
    if (output < cfg->out_min) { output = cfg->out_min; at_min = true; }
    if (output > cfg->out_max) { output = cfg->out_max; at_max = true; }

    // Anti-windup: freeze integral only if saturated AND integral pushes further out.
    // If at min but error>0 (need more heat), integral MUST grow to raise output.
    // If at max but error<0 (need less heat), integral MUST shrink to lower output.
    bool freeze = (at_min && error < 0) || (at_max && error > 0);
    if (!freeze) {
        s->integral = i_raw;
    }

    s->prev_temp = measured;
    return output;
}

#ifdef __cplusplus
}
#endif
