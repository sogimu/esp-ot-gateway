## Why

При срабатывании блокировки цикла (`cycle_locked_`) `PidPollInteractor` записывает в состояние только `pid_output = 25`, но не меняет фактическую уставку CH и не выключает нагрев. Котёл продолжает работать на прежней уставке (например, 26 °C), хотя в логе отображается «блокировка цикла, SP 25». Защита от короткого цикла фактически не выполняется.

## What Changes

- **Блокировка цикла снижает уставку CH**: ветка `cycle_locked_` в `PidPollInteractor::execute()` SHALL записывать `MIN_SETPOINT` (25 °C) в `state_.set_ch_setpoint()` — по аналогии с уже работающей веткой перегрева, которая так делает.

## Capabilities

### New Capabilities

- `pid-control`: поведение PID-контроллера при блокировке цикла горелки.

### Modified Capabilities

<!-- нет изменений существующих спец -->

## Impact

- `main/application/use_cases/pid_poll_interactor.cpp` — ветка `cycle_locked_` дополнительно пишет `MIN_SETPOINT` в уставку CH через `state_.set_ch_setpoint()`.
