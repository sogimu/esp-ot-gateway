## 1. Модель состояния и персист

- [x] 1.1 Добавить `pid_sp_min`/`pid_sp_max` в `IHeatingStateStore` и `HeatingStateAdapter` (+ поля состояния).
- [x] 1.2 Добавить в `FakeHeatingStateStore`.
- [x] 1.3 Загрузка/сохранение в `BoilerNvsStore` (ключи `pid_spmn`/`pid_spmx`).

## 2. Настройка через API

- [x] 2.1 `IConfigurePid::set_pid_flow_limits(min,max)` + реализация в `SystemConfigInteractor`.
- [x] 2.2 Разбор `pid_sp_min`/`pid_sp_max` в `POST /api/control`.

## 3. PID-логика

- [x] 3.1 Убрать комнатный гистерезис; выход PI в `[pid_sp_min, pid_sp_max]`.
- [x] 3.2 Инициализировать интеграл в `pid_sp_min`; интегрировать всегда (кроме DHW/override).
- [x] 3.3 Вкл/выкл CH по выходу с deadband (`pid_hysteresis`).

## 4. Веб

- [x] 4.1 Поля «Мин. подача» / «Макс. подача» в панели PID.
- [x] 4.2 Отправка в `/api/control`, отображение в статусе, обновление подписи deadband.

## 5. Проверка

- [x] 5.1 Обновить/добавить тесты PID (границы, вкл/выкл по выходу, уменьшение интеграла).
- [x] 5.2 Собрать и прогнать тесты.
- [x] 5.3 Обновить README (RU/EN).
