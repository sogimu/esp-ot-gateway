## 1. Доменный объект и сервис

- [x] 1.1 Добавить `DHW_Schedule { bool enabled; bool on[24]; }` в `main/domain/value_objects/ch_schedule.h` рядом с `CH_Schedule`/`PID_Schedule`
- [x] 1.2 Добавить чистую функцию разрешения нагрева `evaluate_dhw(enabled, on, hour) -> bool` (отдельный сервис `DhwScheduleService` или статический метод рядом с `ScheduleService`)

## 2. Применение сетки в контуре управления

- [x] 2.1 В `BoilerPollInteractor::do_dhw_hysteresis()` вычислить `dhw_allowed = dhw_enable && evaluate_dhw(...)` по локальному часу и использовать вместо `dhw_enabled`
- [x] 2.2 Обеспечить немедленный сброс `dhw_priority_` при переходе в «запрещённый» час (ветка `else`)
- [x] 2.3 Логировать смену состояния «нагрев ГВС разрешён/запрещён» коротким событием MODE при смене часа (если принято в Open Questions)

## 3. Хранение в состоянии

- [x] 3.1 Добавить `set_dhw_schedule(const void*)` / `get_dhw_schedule(void*) const` в `IHeatingStateStore`
- [x] 3.2 Реализовать хранение в `HeatingStateAdapter` (поля `dhw_schedule_enabled_`, `dhw_schedule_on_[24]`) и обновить фейки в тестах

## 4. Персистентность в NVS

- [x] 4.1 В `BoilerNvsStore::save_boiler_config()` сохранять blob `dhw_sched` (`DHW_Schedule`), по образцу `ch_sched`/`pid_sched`
- [x] 4.2 В `BoilerNvsStore::load_boiler_config()` читать blob `dhw_sched` двухшаговым запросом размера и вызывать `set_dhw_schedule()`

## 5. Порт и use case

- [x] 5.1 Добавить `set_dhw_schedule(const DHW_Schedule&)` в `IConfigureSystem`
- [x] 5.2 Реализовать в `SystemConfigInteractor` (применение к состоянию + `save_and_log`)

## 6. HTTP API и presenter

- [x] 6.1 Добавить роуты `GET/POST /api/dhw_schedule` в `http_controller_adapter.cpp` (парсинг `enabled` + `on:[...]`)
- [x] 6.2 Добавить `WebPresenterAdapter::render_dhw_schedule()` с полями `enabled`, `hour` (локальный час или `-1`), `on` (24 значения)

## 7. Веб-панель

- [x] 7.1 Добавить панель `pnl_dhw_sched` в `web_page.h` (сетка 12×2 кликабельных ячеек on/off, кнопки «Выбрать все/Снять/Сохранить»), в стиле `pnl_sched`
- [x] 7.2 Добавить JS-логику (init/render/save) с эндпоинтом `/api/dhw_schedule`

## 8. Тесты и проверка

- [x] 8.1 Добавить юнит-тесты `evaluate_dhw` (разрешённый/запрещённый час, выключенное расписание, невалидный час)
- [x] 8.2 Добавить тест применения сетки в `do_dhw_hysteresis` (запрещённый час гасит `dhw_priority_`, мастер-выключатель приоритетнее)
- [x] 8.3 Добавить/обновить тест персиста `DHW_Schedule` (round-trip NVS), если есть оснастка для `BoilerNvsStore`
- [x] 8.4 Собрать проект и прогнать тестовый таргет
