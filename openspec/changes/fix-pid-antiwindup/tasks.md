## 1. Реализация

- [x] 1.1 `PidPollInteractor::compute_pid(bool integrate)`: при `!integrate` замораживать интеграл (`ki = 0`), при `integrate` поднимать интеграл до `out_min − P`.
- [x] 1.2 В `execute()` вычислять `ch_on` до `compute_pid()`; передавать `ch_on`.
- [x] 1.3 Обновить сигнатуру в `pid_poll_interactor.h`.

## 2. Проверка

- [x] 2.1 Собрать host-тесты без ошибок.
- [x] 2.2 Тест: при `ch_on == false` интеграл не растёт (`get_pid_i()` не меняется).
- [x] 2.3 Тест: при включении CH выход сразу выше `out_min` (нет холостого хода).
- [x] 2.4 Прогнать `[pid]`-тесты.
