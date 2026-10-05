## 1. Домен: гейт по модуляции

- [ ] 1.1 В `gas_flow_estimator.cpp execute()` заменить чтение `flame` на `bool firing = mod_raw > FIRING_EPS`; `if (flame)` → `if (firing)`, `else` ветка (`latest_flow_ = 0`) — по `!firing`.
- [ ] 1.2 На переходе `!firing_prev_ && firing` — `kalman_mod_.reset(mod_raw)` и `ignition_start_ms_ = now_ms` (warmup).
- [ ] 1.3 В `gas_flow_estimator.h` переименовать `flame_prev_` → `firing_prev_`, добавить `static constexpr float FIRING_EPS = 0.5f;`, в `reset()` обнулить `firing_prev_`.
- [ ] 1.4 Убрать чтение `state_.is_flame_on()` из `execute()` (сервису flame больше не нужен).

## 2. Тесты

- [ ] 2.1 `test_gas_flow_bugs.cpp`: тест «0% modulation + flame = minimum firing power» заменить на «mod==0 → нулевой расход»; «no flame produces zero flow» → «no modulation produces zero flow»; там, где нужно накопление, ставить `set_modulation(>0)`.
- [ ] 2.2 Перевести на `set_modulation(>0)` вместо `set_flame(true)`: `test_gas_integration.cpp`, `test_gas_daily_tracking.cpp`, `test_gas_meter_persistence.cpp`, `test_reset_statistics.cpp`, `test_gas_model_config.cpp`.
- [ ] 2.3 Добавить регресс-тест: `set_flame(true); set_modulation(0)` → интеграл не растёт.

## 3. Проверка

- [ ] 3.1 Сборка прошивки и host-тесты (`cmake --build build_test -j$(nproc) && ./run_tests -s`).
- [ ] 3.2 Ручная проверка на устройстве: в простое CH (`flame=1, mod=0`) интеграл не растёт; при реальном горении — растёт.
