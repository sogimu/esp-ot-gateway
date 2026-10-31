## 1. Реализация

- [x] 1.1 В `PidPollInteractor::execute()` убрать `!state_.is_ch_enabled()` из условия гистерезиса; дедбенд — по `get_pid_ch_enabled_by_pid()`.
- [x] 1.2 Применить `state_.set_ch_setpoint(ch_on ? output : MIN_SETPOINT)` в том же `lock_exclusive`-блоке, что и `set_pid_state(...)`; убрать `state_.set_ch_enable(true)`.
- [x] 1.3 В `BoilerPollInteractor::build_master_byte()` гасить `MASTER_CH_ENABLE`, когда `is_pid_mode(...) && !get_pid_ch_enabled_by_pid()`.

## 2. Проверка

- [x] 2.1 Собрать host-тесты без ошибок компиляции.
- [x] 2.2 Добавить тест off-пути: после нагрева выше цели `get_pid_ch_enabled_by_pid()==false` и `get_ch_setpoint()==25`.
- [x] 2.3 Прогнать `[pid]`-тесты.
