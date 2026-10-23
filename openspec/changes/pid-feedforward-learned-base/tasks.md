## 1. Реализация

- [x] 1.1 `PidPollInteractor`: добавить `sp_lo_`, `sp_hi_`, `base_`, `room_guard_off_`.
- [x] 1.2 `load_config`: вычислить `sp_lo_/sp_hi_`, инициализировать `base_` (bumpless) и интеграл.
- [x] 1.3 `compute_pid`: `trim = clamp(P+I+D, trim_lo..trim_hi)`, `SP = clamp(base + trim, sp_lo_, sp_hi_)`.
- [x] 1.4 `execute`: сброс интеграла при CH off; обучение базы; комнатный предохранитель.
- [x] 1.5 Лог PID с базой.

## 2. Проверка

- [x] 2.1 Обновить тесты под feed-forward (границы, обучение, bumpless, предохранитель).
- [x] 2.2 Собрать host-тесты и прошивку.
