## Why

`GasFlowService` начисляет расход по биту `flame` OpenTherm: пока флаг установлен, модель считает, что горелка жжёт минимум (`pmin`). На Baxi Duo-tec Compact бит `flame` остаётся «1» во время простоя отопления при выключенной горелке (газовый счётчик стоит на месте), из-за чего виртуальный счётчик систематически завышает расход. Эмпирически на этом котле: `modulation=0` ↔ горелка не горит, `modulation 28–48%` ↔ горит (гистограмма: p25=0, p50=28, p75=p90=48). Значит сигналом «горит» должна быть модуляция, а не flame.

## What Changes

- `GasFlowService::execute()` гейтит накопление по **модуляции** (`mod_raw > ε`), а не по `flame`.
- При переходе 0 → >0 сбрасывается фильтр Калмана модуляции и стартует ramp прогрева (warmup).
- При `mod==0` расход = 0 и объём не накапливается, независимо от бита `flame`.
- Бит `flame` остаётся в состоянии для `BurnCycleService` / `ModulationStatsService` / `DHWPredictService` — они правомерно используют его для своей статистики.
- Тесты газового сервиса переводятся с `set_flame(...)` на `set_modulation(>0)`.

## Capabilities

### New Capabilities

<!-- новых capability нет -->

### Modified Capabilities

- `gas-consumption-history`: критерий «горит» меняется с флага пламени на модуляцию > 0.

## Impact

- `main/application/services/gas_flow_estimator.{h,cpp}` — гейт по модуляции, сброс Калмана, `flame_prev_` → `firing_prev_`, константа ε.
- Тесты: `test_gas_flow_bugs.cpp`, `test_gas_integration.cpp`, `test_gas_daily_tracking.cpp`, `test_gas_meter_persistence.cpp`, `test_reset_statistics.cpp`, `test_gas_model_config.cpp`.
