## Context

`PidPollInteractor::execute()` вычисляет `ch_on` по гистерезису. Флаг `pid_ch_enabled` попадает в веб-презентер, но изначально не влиял на `BoilerPollInteractor::build_master_byte()`: бит CH строился только из `ch_enable`, поэтому котёл продолжал греть после «выключения» PID.

В вебе уже есть семантика «эффективное отопление» = `ch_enable && pid_ch_enabled` в PID-режиме (`web_page.h`), но на котёл она не транслировалась.

## Goals / Non-Goals

**Goals:**
- При `ch_on == false` котёл реально прекращает нагрев: бит CH master-байта гасится.
- Уставка CH остаётся результатом PI и не подменяется на `MIN_SETPOINT`.
- Мастер-переключатель `ch_enable` остаётся за пользователем (Разрешение).

**Non-Goals:**
- Не меняем логику перегрева, блокировки цикла и DHW-приоритета.
- Не вводим новый флаг «ручное выключение»: `ch_enable` уже выполняет эту роль.

## Decisions

- Гистерезис: дедбенд помнит своё решение через `pid_ch_enabled_by_pid`, а не через глобальный `ch_enable` (иначе после первого же `ch_on=false` PID не смог бы включиться обратно).
- Применение: `pid_ch_enabled` передаётся в `set_pid_state()`; `set_ch_setpoint(output)` продолжает писаться и при `ch_on == false`. PID больше не вызывает `set_ch_enable(...)`.
- Гейт в `build_master_byte()`: `ch_en &&= !(is_pid_mode && !pid_ch_enabled)`.

## Risks / Trade-offs

- [На старте в PID-режиме `pid_ch_enabled` = false до первого расчёта (~dt_sec)] → Котёл не греет первую минуту после загрузки; приемлемо, PID прогревается.
- [Чтение `pid_ch_enabled`/`ch_mode` в `build_master_byte()` без лока] → Атомарный bool/int, гонка некритична; при желании можно обернуть в `lock_shared`.
- [Котёл игнорирует CH enable] → защиты низкой уставкой нет. Принято: на проверенном Baxi бит работает.
