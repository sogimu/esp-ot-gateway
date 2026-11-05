## 1. Реализация

- [x] 1.1 В `PidPollInteractor::execute()` убрать `!state_.is_ch_enabled()` из условия гистерезиса; дедбенд — по `get_pid_ch_enabled_by_pid()`.
- [x] 1.2 При `ch_on == false` сбрасывать только `pid_ch_enabled`; уставку CH не парковать на `MIN_SETPOINT`, а продолжать писать результат PI.
- [x] 1.3 В `BoilerPollInteractor::build_master_byte()` гасить `MASTER_CH_ENABLE`, когда `is_pid_mode(...) && !get_pid_ch_enabled_by_pid()`.

## 2. Проверка

- [x] 2.1 Собрать host-тесты без ошибок компиляции.
- [x] 2.2 Добавить тест off-пути: после нагрева выше цели `get_pid_ch_enabled_by_pid()==false`, а `get_ch_setpoint()` остаётся результатом PI.
- [x] 2.3 Прогнать `[pid]`-тесты.
