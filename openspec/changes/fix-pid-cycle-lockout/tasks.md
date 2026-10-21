## 1. Реализация

- [x] 1.1 В `PidPollInteractor::execute()`, в ветке `cycle_locked_` (`pid_poll_interactor.cpp:139-148`), добавить `state_.set_ch_setpoint(MIN_SETPOINT)` внутри уже взятого `lock_exclusive`-блока после `set_pid_state(...)`.

## 2. Проверка

- [x] 2.1 Собрать проект (host-тесты/линковка), убедиться, что нет ошибок компиляции.
- [x] 2.2 Добавить/обновить тест в `test/test_pid_poll_interactor.cpp`, проверяющий, что при активной блокировке цикла `get_ch_setpoint() == MIN_SETPOINT`.
