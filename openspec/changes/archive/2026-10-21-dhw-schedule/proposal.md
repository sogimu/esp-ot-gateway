## Why

Ночью греть бойлер ГВС нет смысла: вода в баке держит температуру, а котёл тратит газ впустую. Сейчас у ГВС есть только мастер-выключатель (`dhw_enable`) и уставка с гистерезисом — возможности «не топить в заданные часы суток» нет. Для отопления (CH) такая суточная сетка уже есть, и логично дать тот же механизм бойлеру.

## What Changes

- Добавить суточное расписание нагрева ГВС: 24 часовые ячейки on/off (`DHW_Schedule { enabled, on[24] }`) — в «разрешённые» часы бойлер греется по гистерезису, в «запрещённые» нагрев выключен.
- Применять сетку в контуре управления по локальному часу, по образцу существующего `do_schedule()` для CH.
- Персистить `DHW_Schedule` (включая флаг `enabled`) в NVS, наравне с `CH_Schedule`/`PID_Schedule`.
- Добавить HTTP-эндпоинт `GET/POST /api/dhw_schedule` и рендер в презентере, по образцу `/api/schedule`.
- Добавить веб-панель редактирования: сетка 24 кликабельных ячеек on/off (без слайдера температуры) + «Выбрать все / Снять / Сохранить», визуально в стиле существующей панели CH-расписания.
- Поведение при несинхронизированном времени — fail-safe: сетка не применяется, нагрев работает как сейчас (`dhw_enable` + гистерезис), чтобы сбой часов не оставил бак холодным.

## Capabilities

### New Capabilities

- `dhw-schedule`: суточное расписание нагрева ГВС — применение почасового on/off, персистентность, API и согласованный интерфейс редактирования.

### Modified Capabilities

<!-- нет изменений требований существующих спец -->

## Impact

- `main/domain/value_objects/ch_schedule.h` — новый `DHW_Schedule` рядом с `CH_Schedule`/`PID_Schedule`.
- `main/application/use_cases/boiler_poll_interactor.cpp` — применение сетки в `do_dhw_hysteresis()` / `build_master_byte()` (эффективный `dhw_enable` по часу).
- `main/application/ports/driven/iheating_state_store.h` + `heating_state_adapter` — хранение `DHW_Schedule` в памяти.
- `main/infrastructure/driven/boiler_nvs_store.{h,cpp}` — blob `dhw_sched`.
- `main/application/ports/driving/iconfigure_system.h` + `system_config_interactor` — `set_dhw_schedule`.
- `main/infrastructure/driving/http_controller_adapter.cpp` — роуты `/api/dhw_schedule`.
- `main/infrastructure/driven/web_presenter_adapter.cpp` — `render_dhw_schedule`.
- `main/infrastructure/driving/web_page.h` — панель расписания ГВС.
- Тесты: применение on/off, поведение при несинхронизированном времени, персистентность.
