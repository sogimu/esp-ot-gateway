## Context

`PidPollInteractor::execute()` вычисляет `ch_on` по гистерезису и пишет уставку/флаг только при `ch_on == true` (`pid_poll_interactor.cpp:185-191`). Ветки «выключить» нет. Флаг `pid_ch_enabled` попадает только в веб-презентер, но не в управление котлом: `BoilerPollInteractor::build_master_byte()` строит бит CH только из `ch_enable`.

Вебе уже есть семантика «эффективное отопление» = `ch_enable && pid_ch_enabled` в PID-режиме (`web_page.h`, `chEna=d.ch_enable&&(d.ch_mode==1?d.pid_ch_enabled:true)`), но на котёл она не транслируется.

## Goals / Non-Goals

**Goals:**
- При `ch_on == false` котёл реально прекращает нагрев: уставка → 25 °C, бит CH master-байта гасится.
- Мастер-переключатель `ch_enable` остаётся за пользователем (Разрешение).

**Non-Goals:**
- Не меняем логику перегрева, блокировки цикла и DHW-приоритета.
- Не вводим новый флаг «ручное выключение»: `ch_enable` уже выполняет эту роль.

## Decisions

- Гистерезис: дедбенд помнит своё решение через `pid_ch_enabled_by_pid`, а не через глобальный `ch_enable` (иначе после первого же `ch_on=false` PID не смог бы включиться обратно).
- Применение: `set_ch_setpoint(ch_on ? output : MIN_SETPOINT)`. PID больше не вызывает `set_ch_enable(...)`.
- Гейт в `build_master_byte()`: `ch_en &&= !(is_pid_mode && !pid_ch_enabled)`.

## Risks / Trade-offs

- [На старте в PID-режиме `pid_ch_enabled` = false до первого расчёта (~dt_sec)] → Котёл не греет первую минуту после загрузки; приемлемо, PID прогревается.
- [Чтение `pid_ch_enabled`/`ch_mode` в `build_master_byte()` без лока] → Атомарный bool/int, гонка некритична; при желании можно обернуть в `lock_shared`.
