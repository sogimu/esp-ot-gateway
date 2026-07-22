#pragma once

/// Driven-порт: персистентность истории предсказания ГВС (DHW predict).
/// Выделен из IConfigurationStore/NvsConfigStore — DHWPredictService
/// обращается к NVS только через этот интерфейс.
class IPredictStore {
public:
    virtual ~IPredictStore() = default;

    /// Загрузить историю сессий (3 скорости + индекс + счётчик).
    /// true = данные есть (хотя бы один параметр загружен).
    virtual bool load_predict(float rates[3], int& idx, int& cnt) = 0;

    /// Сохранить историю сессий в NVS.
    virtual void save_predict(const float rates[3], int idx, int cnt) = 0;
};
