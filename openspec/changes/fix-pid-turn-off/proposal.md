## Why

`PidPollInteractor` должен выключать котёл, когда комната достигает цели. Раньше при `ch_on == false` PID обновлял только флаг `pid_ch_enabled` (виден в вебе как «CH откл»), но `BoilerPollInteractor::build_master_byte()` не учитывал его при сборке master-байта, поэтому котёл продолжал греть на последней уставке.

Первая версия фикса дополнительно опускала уставку CH до `MIN_SETPOINT` (25 °C). Это оказалось лишним: бит CH надёжно управляет котлом, а скачок уставки на 25 °C не является результатом PI и вводит в заблуждение. Уставка должна продолжать следовать за расчётом PI.

## What Changes

- **`build_master_byte()`**: в адаптивном (PID) режиме бит CH включения (`MASTER_CH_ENABLE`) гасится, когда `pid_ch_enabled == false`.
- **`PidPollInteractor::execute()`**: гистерезис использует `pid_ch_enabled_by_pid` как память дедбенда; при `ch_on == false` сбрасывается только `pid_ch_enabled`, уставка CH остаётся результатом PI.
- PID не пишет `set_ch_enable(...)`: мастер-переключатель `ch_enable` остаётся за пользователем, а решение PID учитывается в `build_master_byte()`.

## Capabilities

### New Capabilities

- `pid-control`: off-путь PID-контроллера (гашение бита CH при выключении по гистерезису).

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/pid_poll_interactor.cpp` — гистерезис и запись флага при `ch_on == false`; уставка не паркуется.
- `main/application/use_cases/boiler_poll_interactor.cpp` — `build_master_byte()` гасит CH в PID-режиме при `pid_ch_enabled == false`.
- `test/test_pid_poll_interactor.cpp` — тест off-пути PID.
