## 1. Применение почасовой уставки CH

- [x] 1.1 Добавить поле `last_applied_hour_` (инициализация -1) в `BoilerPollInteractor` (`.h`)
- [x] 1.2 Добавить приватный метод `do_schedule()` в `BoilerPollInteractor` (`.h`/`.cpp`) по алгоритму D1: выход вне `Manual_Sched`, при выключенном расписании, при недостоверном времени, при неизменном часе
- [x] 1.3 Подключить `ScheduleService::evaluate()` и вызывать `set_ch_setpoint()` при `has_setpoint`
- [x] 1.4 Вызвать `do_schedule()` в `execute()` между `do_dhw_hysteresis()` и `do_status()`
- [x] 1.5 Логировать смену почасовой уставки коротким событием MODE при изменении часа

## 2. Персистентность CH_Schedule в NVS

- [x] 2.1 В `BoilerNvsStore::save_boiler_config()` сохранять blob `ch_sched` (`CH_Schedule`), по образцу `pid_sched`
- [x] 2.2 В `BoilerNvsStore::load_boiler_config()` читать blob `ch_sched` двухшаговым запросом размера и вызывать `set_schedule()`

## 3. Поле `hour` в ответе API расписания

- [x] 3.1 Добавить вычисление локального часа в `WebPresenterAdapter::render_schedule()` (по образцу `render_pid_schedule()`), возвращать `-1` при недостоверном времени
- [x] 3.2 Включить `"hour"` в JSON-ответ `render_schedule()`

## 4. UI панели расписания

- [x] 4.1 В `web_page.h` привести подпись «Температура» → «Уставка CH» с тултипом в стиле `ch_sp`
- [x] 4.2 Выровнять порядок/классы виджетов (кнопки −/+, ползунок, значение с `&deg;C`) под `pnl_manual`
- [x] 4.3 Проверить, что статусная строка `sched_status` корректно показывает час и уставку после появления `d.hour`

## 5. Тесты и проверка

- [x] 5.1 Добавить юнит-тест применения `ScheduleService::evaluate()` (валидный час, выключенное расписание, невалидный час, границы)
- [x] 5.2 Добавить/обновить тест персиста `CH_Schedule` (round-trip NVS), если есть тестовая оснастка для `BoilerNvsStore`
- [x] 5.3 Собрать проект (`idf.py build` или аналог) и прогнать тестовый таргет
