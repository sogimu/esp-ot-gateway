## Context

В проекте уже есть вся инфраструктура под ручное расписание CH: доменный `CH_Schedule`, `ScheduleService::evaluate()`, хранение в `HeatingStateAdapter` (`schedule_enabled_`, `schedule_temps_[24]`), HTTP-эндпоинты `/api/schedule` (GET/POST) и UI-панель `pnl_sched`. Но звено, которое применяет почасовую уставку к котлу, отсутствует:

- `ScheduleService::evaluate()` нигде не вызывается (мёртвый код).
- `CH_Schedule` не сохраняется в NVS (сохраняется только `PID_Schedule` под ключом `pid_sched`).
- `render_schedule()` не возвращает `hour`, поэтому статусная строка в вебе всегда показывает «NTP...».

При этом аналог для PID уже работает: `PidPollInteractor::execute()` при `CHMode::PID_Sched` читает `PID_Schedule`, вычисляет локальный час и обновляет цель комнаты (`pid_poll_interactor.cpp:81-95`). Нужно зеркально повторить это для ручного режима, но менять уставку CH.

Запись уставки CH в котёл уже делает `BoilerPollInteractor::do_extra_step()` (case 1/10): пишет `state_.get_ch_setpoint()` (либо `pending_ch_sp_`, если выставлен `pending_ch_sp_dirty_`) в OpenTherm ID 1.

## Goals / Non-Goals

**Goals:**
- Применять почасовую уставку CH по локальному времени, когда активен `Manual_Sched` и расписание включено.
- Сохранять/восстанавливать `CH_Schedule` в NVS.
- Возвращать `hour` в `GET /api/schedule`.
- Выровнять UI панели расписания под дизайн ручного статичного режима.

**Non-Goals:**
- Не трогаем уже работающий `PID_Sched`.
- Не меняем семантику `Manual_Static` и границы клампинга (это было в `fix-ch-heating-control`).
- Не добавляем новые точки входа/API — только доработка существующих.
- Не вводим редактирование графика «с минутами» — остаёмся на почасовой сетке 0–23.

## Decisions

### D1: Где применять расписание

Применение уставки CH помещаем в `BoilerPollInteractor` (владелец записи CH-уставки в котёл), добавляя шаг `do_schedule()` в `execute()` — сразу после `do_dhw_hysteresis()` и до `do_status()`.

Алгоритм `do_schedule()`:
1. Если `state_.get_ch_mode() != CHMode::Manual_Sched` — очистить кэш «последнего применённого часа» и выйти.
2. Прочитать `CH_Schedule` через `state_.get_schedule(&sched)`.
3. Если `!sched.enabled` — выйти (оставить текущую уставку).
4. Если `!time_.is_synced()` — выйти (не применяем по недостоверному времени).
5. Вычислить `hour = duration_cast<hours>(time_.local_now().time_since_epoch()).count() % 24`.
6. Если `hour == last_applied_hour_` — выйти (не перезаписывать каждую итерацию ~1.1 с).
7. `auto r = ScheduleService::evaluate(sched.enabled, sched.temps, hour);` — если `r.has_setpoint`, вызвать `set_ch_setpoint(r.setpoint)` (штатный pending-путь) и запомнить `last_applied_hour_ = hour`.

**Почему здесь, а не отдельный interactor:** `BoilerPollInteractor` уже владеет каналом записи CH-уставки и уже имеет `ITimeSource`. Отдельный `SchedulePollInteractor` добавил бы ещё одну сущность в control loop ради нескольких строк и потребовал бы проброски `BoilerPollInteractor*` (как уже сделано в `SystemConfigInteractor`) либо дублирования логики записи.

**Альтернатива (отклонена):** отдельный `IControlTask` — чище по SRP, но избыточен для текущего объёма; можно выделить позже, если расписание обрастёт логикой (например, переходы с интерполяцией).

### D2: Переиспользование `ScheduleService`

Подключаем существующий `ScheduleService::evaluate()` вместо новой логики. Его `is_valid_setpoint()` (20–80 °C) остаётся грубой страховкой; реальные границы уже обеспечены клампингом в `SystemConfigInteractor::set_schedule()` (через `clamp_ch`, который использует границы ID 49). Двойной контур защиты сохраняем.

### D3: Персистентность `CH_Schedule`

Зеркалим существующую схему `PID_Schedule`:
- В `BoilerNvsStore::load_boiler_config()` — читать blob `ch_sched` (двухшаговый запрос размера, `sz == sizeof(CH_Schedule)`), затем `s.set_schedule(&cs)`.
- В `BoilerNvsStore::save_boiler_config()` — `CH_Schedule cs; s.get_schedule(&cs); nvs_set_blob(h, "ch_sched", &cs, sizeof(cs));`.

Ключ `ch_sched` не пересекается с `pid_sched`. Формат совместим с версией `cfg_ver` = 1 (новые ключи не ломают старые прошивки; отсутствие ключа → дефолт `enabled=false`, нулевые температуры).

**Примечание:** `save_boiler_config` принимает `const IHeatingStateStore&`; `get_schedule` объявлен `const` — ограничений нет.

### D4: Поле `hour` в `render_schedule()`

Копируем блок вычисления локального часа из `render_pid_schedule()` (`web_presenter_adapter.cpp:187-192`), но возвращаем `-1` при недостоверном времени. `render_schedule` получает доступ к `time_` (уже есть в презентере) — менять сигнатуру не нужно.

### D5: UI панели расписания

Приводим `pnl_sched` в `web_page.h` к виду `pnl_manual`:
- Подпись «Температура» → «Уставка CH» с тултипом в том же стиле, что у `ch_sp`.
- Сохраняем порядок виджетов: `−` → ползунок → `+` → значение с `&deg;C` (уже совпадает), выравниваем отступы/классы под `pnl_manual`.
- Остальные кнопки («Установить», «Выбрать все», «Снять», «Сохранить») и сетка часов не меняются.

Чисто декларативная правка разметки; JS-логика (`schedPoll`, `schedSet`, `schedSave`) не меняется, кроме уже исправленного чтения `d.hour`.

## Risks / Trade-offs

- **[Частая запись уставки]** Если не кэшировать `last_applied_hour_`, уставка будет перезаписываться каждый тик (~1.1 с) → гонки с `pending_ch_sp_dirty_` и лишние OT-записи. → Митигировано кэшем «последнего применённого часа» (D1, шаг 6).
- **[Недостоверное время]** При `!is_synced()` расписание молча не применяется, котёл остаётся на последней уставке. → Сознательное поведение (сохранение последней корректной уставки безопаснее, чем установка произвольной по «нулевому» часу).
- **[Гонки записи CH-уставки]** `BoilerPollInteractor::set_ch_setpoint()` ставит `pending_ch_sp_dirty_`, а `do_extra_step()` может в тот же момент писать `state_.get_ch_setpoint()`. Существующий single-threaded control loop (все задачи в одном `ControlLoopInteractor::run_once()`) устраняет одновременный доступ; смена часа происходит между итерациями. → Изменений в блокировках не требуется.
- **[Миграция NVS]** Новый ключ `ch_sched` добавляется без повышения `cfg_ver`. Отсутствие ключа корректно обрабатывается (дефолт). → Обратной несовместимости нет.

## Open Questions

- Нужно ли логировать применение почасовой уставки в журнал событий (как это делается для `set_ch_setpoint`)? Предварительно — да, коротким событием уровня MODE при смене часа, чтобы диагностировать работу режима.
