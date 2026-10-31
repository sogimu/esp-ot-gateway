## Why

`PidPollInteractor` не умеет выключать котёл: когда комната достигает цели и гистерезис даёт `ch_on = false`, PID обновляет только флаг `pid_ch_enabled` (виден в вебе как «CH откл»), но не снижает уставку CH и не гасит бит включения. Котёл продолжает греть на последней уставке — в вебе «отопление запрещено», а горелка и насос работают.

## What Changes

- **`build_master_byte()`**: в адаптивном (PID) режиме бит CH включения (`MASTER_CH_ENABLE`) дополнительно гасится, когда `pid_ch_enabled == false`.
- **`PidPollInteractor::execute()`**: гистерезис использует `pid_ch_enabled_by_pid` как память дедбенда (без `!is_ch_enabled()`); при `ch_on == false` уставка CH опускается до `MIN_SETPOINT` (25 °C).
- PID больше не пишет `set_ch_enable(...)`: мастер-переключатель `ch_enable` остаётся за пользователем, а решение PID учитывается в `build_master_byte()`.

## Capabilities

### New Capabilities

- `pid-control`: полный off-путь PID-контроллера (снижение уставки и гашение CH при выключении по гистерезису).

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/pid_poll_interactor.cpp` — гистерезис и запись уставки/флага при `ch_on == false`.
- `main/application/use_cases/boiler_poll_interactor.cpp` — `build_master_byte()` гасит CH в PID-режиме при `pid_ch_enabled == false`.
- `test/test_pid_poll_interactor.cpp` — тест off-пути PID.
