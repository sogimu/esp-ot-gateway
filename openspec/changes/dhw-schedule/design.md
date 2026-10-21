## Context

Инфраструктура суточного расписания CH уже есть и работает: доменные `CH_Schedule`/`PID_Schedule` (`domain/value_objects/ch_schedule.h`), применение по локальному часу в `BoilerPollInteractor::do_schedule()`, HTTP `/api/schedule`, персистентность в NVS (`ch_sched`/`pid_sched`), веб-панель `pnl_sched`. Для ГВС (бойлер/БКН) расписания нет — есть только мастер `dhw_enable`, уставка и гистерезис.

Нагрев ГВС решается в цепочке из двух мест в `boiler_poll_interactor.cpp`:

```
do_dhw_hysteresis()                      build_master_byte()  (вызывается из do_status())
──────────────────                       ────────────────────
dhw_enabled ─┐                           dhw_en ────────────┐
             ├─▶ dhw_priority_ (bool)    dhw_priority_ ────┼─▶ MASTER_DHW_ENABLE
температура ─┘  (переключается гистерезисом)                └─▶ MASTER_CH2_ENABLE
```

`execute()` вызывает `do_dhw_hysteresis()` **до** `do_status()`, поэтому достаточно управлять `dhw_priority_` — `build_master_byte()` читает его уже обновлённым.

## Goals / Non-Goals

**Goals:**
- Дать суточное расписание нагрева ГВС: 24 часовые ячейки on/off.
- Применять его по локальному часу с учётом часового пояса.
- Сохранять/восстанавливать `DHW_Schedule` в NVS.
- Дать HTTP `GET/POST /api/dhw_schedule` и веб-панель в стиле CH-расписания.
- Поведение при недостоверном времени — fail-safe (греть, не морозить).

**Non-Goals:**
- Не трогаем CH/PID-расписания и их UI.
- Не меняем семантику `dhw_enable`, уставки и гистерезиса.
- Не вводим недельную сетку (7×24), интервалы с минутами или «отпускной» режим — только суточная почасовая сетка.
- Не выводим расписание в MQTT/Home Assistant (как и суточные CH-расписания — только HTTP/веб).

## Decisions

### D1: Модель данных — режим `DHWMode` + сетка `bool on[24]`

Режим задаёт, применяется ли расписание, — по образцу `CHMode` (подрежим «Статичный / По расписанию»):

```cpp
enum class DHWMode : int {
    Static   = 0,   // постоянное разрешение нагрева (по dhw_enable)
    Schedule = 1    // разрешение нагрева по суточному расписанию
};

struct DHW_Schedule {
    bool on[24];
    DHW_Schedule() { for (int i = 0; i < 24; i++) on[i] = true; } // fail-safe: нагрев разрешён по умолчанию
};
```

Кейс «ночью не греть» требует именно on/off, а не уставки. Тип отличается от `CH_Schedule` (bool vs float), но форма — сетка 24 ячейки — сохраняется. Отдельного флага `enabled` у расписания нет: активация задаётся режимом `DHWMode::Schedule`.

**Альтернатива (отклонена):** отдельный флаг `enabled` у `DHW_Schedule` — дублирует режим (как избыточный `enabled` у CH) и не выражает «режим» явно в интерфейсе.

### D2: Где применять — внутри `do_dhw_hysteresis()`

Вводим эффективное разрешение нагрева и подставляем вместо `dhw_enabled`:

```
schedule_ok = (dhw_mode == Schedule) ? is_allowed(on, local_hour) : true
dhw_allowed = dhw_enable && schedule_ok
```

- При `on[h]=false` ветка `else` в `do_dhw_hysteresis()` сбрасывает `dhw_priority_ = false` → нагрев останавливается сразу.
- `build_master_byte()` не трогаем: он уже требует `dhw_en && dhw_priority_`.
- `dhw_enable=false` остаётся мастером — режим/расписание не «воскрешает» нагрев.

Вычисление `on[local_hour]` выносим в чистую функцию `DhwScheduleService::is_allowed(on, hour) -> bool allowed`.

### D3: Fail-safe при недостоверном времени

Как в `do_schedule()` для CH: если `!time_.is_synced()` — сетку не применяем, нагрев работает как раньше (`dhw_enable` + гистерезис). Сбой часов не должен оставить бак холодным.

### D4: Персистентность `DHWMode` и `DHW_Schedule`

- `dhw_mode` — ключ NVS `dhw_mode` (i32), по образцу `ch_mode`.
- `DHW_Schedule` — blob `dhw_sched` (двухшаговый `nvs_get_blob` + проверка `sz == sizeof(DHW_Schedule)`), по образцу `ch_sched`/`pid_sched`.

Новые ключи не ломают старые прошивки (отсутствие → дефолт `Static` / все ячейки `true`), `cfg_ver` не повышаем.

### D5: HTTP + presenter

- Роут `GET/POST /api/dhw_schedule` в `http_controller_adapter.cpp` (парсинг `on:[...]`, в отличие от `temps:[...]` у CH).
- `WebPresenterAdapter::render_dhw_schedule()` — возвращает `{"hour":…,"on":[0,1,…]}`; `hour` = локальный час или `-1` при недостоверном времени.
- Режим `dhw_mode` — в `POST /api/control` (как `ch_mode`) и в `GET /api/status` (`render_status`).
- `IConfigureSystem::set_dhw_mode()` / `set_dhw_schedule()` + реализация в `SystemConfigInteractor` (валидация не требуется — булевы ячейки), сохранение через `save_and_log`.

### D6: Веб-панель

В секции «Бойлер БКН» добавляем выбор режима «Статичный / По расписанию» (радио-группа, как подрежим CH), под ней — сетка 12×2 кликабельных ячеек on/off с кнопками «Включить все/Выключить все/Сохранить» (без слайдера температуры). Сетка видна только в режиме «По расписанию». JS — по образцу `schedInit/schedRender/schedSave`, эндпоинт `/api/dhw_schedule`.

## Risks / Trade-offs

- **[Остановка нагрева в середине часа]** Переход в «запрещённый» час гасит `dhw_priority_` немедленно, даже если бак недогрет. → Это и есть требуемое поведение («ночью не топить»); температура воды при этом сохраняется в баке.
- **[Недостоверное время]** При `!is_synced()` расписание молча не применяется. → Сознательный fail-safe (см. D3), согласован с CH.
- **[Несинхронизированные сущности]** Новое поле в `IHeatingStateStore`/`HeatingStateAdapter` требует правки интерфейса и фейков в тестах. → Держим добавку минимальной (одна пара set/get).
- **[NVS-миграция]** Новый ключ `dhw_sched` без повышения `cfg_ver`. → Отсутствие ключа корректно обрабатывается дефолтом, обратной несовместимости нет.

## Open Questions

- Нужно ли логировать смену состояния «нагрев ГВС разрешён/запрещён» в журнал событий (уровень MODE) при смене часа, как это делает `do_schedule()` для CH? Предварительно — да, для диагностики.
- Стоит ли выделять чистую `DhwScheduleService` отдельным файлом или добавить статический метод рядом с `ScheduleService`? Предварительно — отдельный сервис по аналогии.
