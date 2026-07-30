#pragma once

#include "application/ports/driven/ipredict_store.h"

/// NVS-backed хранилище истории предсказания ГВС.
/// Использует namespace "predict", ключ "dhw_hist".
/// Выделен из NvsConfigStore.
class PredictNvsStore : public IPredictStore {
public:
    bool load_predict(float rates[3], int& idx, int& cnt) override;
    void save_predict(const float rates[3], int idx, int cnt) override;
};
