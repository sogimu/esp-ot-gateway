#pragma once
#include <cstdint>

class IHeatingStateStore;

static constexpr int GAS_HOURS_PER_DAY = 24;
static constexpr int GAS_DAILY_SLOTS   = 64;   // ~2 месяца завершённых суток

struct GasDailyEntry {
    int64_t epoch_day;
    float   m3;
};

/// «Сегодня» — частый маленький blob (пишется каждые ~10 мин):
/// переживает ребут без потери текущего часа и завершённых часов сегодня.
struct GasTodayBlob {
    int64_t today_epoch_day;
    float   today_m3;                          // накоплено за сегодня
    float   today_hours[GAS_HOURS_PER_DAY];    // завершённые часы сегодня
    int32_t current_hour;                      // 0..23, -1 = не инициализировано
    float   current_hour_m3;                   // текущий (незавершённый) час
};

/// «История» — редкий большой blob (пишется на границе суток):
/// завершённые сутки + вчерашние часы.
struct GasHistoryBlob {
    GasDailyEntry daily[GAS_DAILY_SLOTS];
    int32_t head;                              // индекс самого старого дня
    int32_t count;                             // заполнено завершённых дней
    int64_t yesterday_epoch_day;
    float   yesterday_hours[GAS_HOURS_PER_DAY];
};

/// Driven-порт: персистентность данных газовой коррекции (журнал сверки счётчика)
/// и калибровки котла, которую меняет GasCorrectionInteractor.
///
/// Выделен из NvsConfigStore — аналогично BoilerNvsStore. Сохранение котловой
/// конфигурации (k_calib, gas_calorific, …) делегируется IBoilerConfigStore
/// (без дублирования логики).
class IGasCorrectionStore {
public:
    virtual ~IGasCorrectionStore() = default;

    /// Загрузить журнал сверки (NvsMeterBlob) из NVS. true = данные есть.
    virtual bool load_meter(IHeatingStateStore& state, void* blob = nullptr) = 0;

    /// Сохранить журнал сверки в NVS.
    virtual void save_meter(const IHeatingStateStore& state, const void* blob = nullptr) = 0;

    /// Сохранить integral_m3 в NVS (stats namespace).
    virtual void save_integral(float value) = 0;

    /// Сохранить котловую конфигурацию (калибровку) — делегирует IBoilerConfigStore.
    virtual void save_boiler_config(const IHeatingStateStore& state) = 0;

    /// «Сегодня + текущий час» — частое сохранение.
    virtual void save_today_gas(const void* blob)   { (void)blob; }
    virtual bool load_today_gas(void* blob)         { (void)blob; return false; }

    /// Завершённая история (сутки + вчерашние часы) — раз в сутки.
    virtual void save_history_gas(const void* blob) { (void)blob; }
    virtual bool load_history_gas(void* blob)       { (void)blob; return false; }
};
