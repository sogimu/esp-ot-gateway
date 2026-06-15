#pragma once
#include <cstdint>

/// Один минутный отсчёт. Флаги упакованы в uint8_t для экономии RAM.
struct __attribute__((packed)) PidQualitySample {
    uint16_t minute_of_day;  // 0–1439
    int16_t  room_temp;      // x100 (20.00°C = 2000)
    int16_t  target_temp;    // x100
    int16_t  outside_temp;   // x100
    uint8_t  flags;          // bit0=flame, bit1=dhw_active, bit2=cycle_locked, bit3=clamped
};

/// Скоры 0–100 по каждой оси + композит.
struct QualityScores {
    float overshoot;      // 100 = нет перерегулирования
    float steady_state;   // 100 = идеальное удержание target
    float stability;      // 100 = нет осцилляций
    float cycling;        // 100 = редкие циклы горелки
    float clamp;          // 100 = выход PID не упирается в границы
    float composite;      // взвешенное среднее
};

/// Результат оценки FOPDT-параметров по одному событию нагрева.
struct FopdtEvent {
    float gain;                // °C комнаты / °C setpoint
    float tau_heat_sec;        // постоянная времени нагрева
    float tau_cool_sec;        // постоянная времени остывания
    float dead_time_sec;       // транспортная задержка
    float outside_temp;        // уличная температура во время события
    bool  valid;               // false если событие прервано DHW
};

// ── Инлайн-функции для конвертации fixed-point ─────────

inline int16_t to_fixed16(float v) {
    return static_cast<int16_t>(v * 100.0f);
}

inline float from_fixed16(int16_t v) {
    return static_cast<float>(v) / 100.0f;
}

// ── Инлайн-функции для доступа к флагам ────────────────

inline bool sample_has_flame(const PidQualitySample& s)   { return s.flags & 0x01; }
inline bool sample_has_dhw(const PidQualitySample& s)     { return s.flags & 0x02; }
inline bool sample_is_locked(const PidQualitySample& s)   { return s.flags & 0x04; }
inline bool sample_is_clamped(const PidQualitySample& s)  { return s.flags & 0x08; }

static_assert(sizeof(PidQualitySample) == 9, "PidQualitySample must be 9 bytes");
