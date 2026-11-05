## Why

Выученная база страдала дрейфом («трещотка»): короткие провалы (дверь/окно) сдвигали её вверх, и после догрева она не возвращалась. Причина — база зависит от фактического времени работы горелки и накапливает транзиенты. Погодная база по уличной температуре (уже приходит от котла) устраняет обучение и дрейф: подача задаётся сразу по погоде, а PI-поправка подчищает остаток.

## What Changes

- **Погодная база**: `base` вычисляется линейной интерполяцией между двумя точками: `Tout = −20 °C → base = 30 °C`, `Tout = +15 °C → base = 25 °C`, с клампом в `[pid_sp_min, pid_sp_max]`.
- **Убираем обучение базы**: `BASE_TAU`, слив, симметричный перенос, `base_inited_`, `last_auto_output_`.
- **Fallback**: если уличная температура недостоверна (NaN / вне −60…+60 / нет источника) — `base = 26 °C`.
- **Настройка**: две уставки кривой `wc_sp_cold` (при −20 °C, дефолт 30) и `wc_sp_warm` (при +15 °C, дефолт 25) — в NVS, `/api/control`, вебе (два ползунка).
- Сохраняем: PI-поправку ±5 с anti-windup, комнатный гистерезис, перегрев, цикловую блокировку, заморозку при ГВС, `pid_sp_min/max`.

## Capabilities

### New Capabilities

- `pid-control`: погодная база по уличной температуре.

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/pid_poll_interactor.{h,cpp}` — расчёт базы по кривой, удаление обучения.
- `main/application/ports/driven/iheating_state_store.h`, `heating_state_adapter.{h,cpp}`, `boiler_nvs_store.cpp` — настройки кривой.
- `main/application/ports/driving/iconfigure_pid.h`, `system_config_interactor.{h,cpp}`, `http_controller_adapter.cpp` — настройка кривой.
- `main/infrastructure/driven/web_presenter_adapter.cpp`, `web_page.h` — отображение и ползунки.
- `test/fakes/fake_heating_state_store.h`, `test/test_pid_poll_interactor.cpp` — тесты.
