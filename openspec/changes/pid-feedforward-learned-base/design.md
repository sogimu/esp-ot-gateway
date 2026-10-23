## Context

`compute_pid` сейчас: `out = clamp(P+I+D, sp_min, sp_max)`, интеграл несёт рабочую точку. Нужно разделить: `base` (learned) + `trim` (PI).

## Goals / Non-Goals

**Goals:**
- `SP = base + trim`, `trim` ограничен ±5 °C.
- База обучается переносом интеграла при стабильной комнате.
- Per-cycle bumpless (интеграл сбрасывается при CH off, база сохраняется).
- Вкл/выкл CH по комнатному гистерезису.

**Non-Goals:**
- Не добавляем погодную кривую и не меняем `sp_min/sp_max`.
- Не выносим `base`/`trim_max` в NVS/UI (пока внутренние константы).

## Decisions

- `pid_step` работает в единицах `trim`: `out_min = −trim_max`, `out_max = +trim_max`, дополнительно урезанных по `sp_min − base` / `sp_max − base`.
- `base_` — член `PidPollInteractor`, инициализируется в `load_config` из `last_auto_output_` (bumpless) или `sp_min`.
- Обучение: `transfer = integral * dt / 1200`; `base += transfer; integral -= transfer` (SP непрерывна) при комнате у цели или насыщении поправки сверху; не во время ГВС.
- При `ch_prev == false` (CH был выключен) интеграл обнуляется перед расчётом.
- Вкл/выкл CH: только комнатный гистерезис по полю `pid_hysteresis` (off при `цель + hyst`, on при `цель − hyst`, защёлка в полосе); отдельного дедбенда по уставке подачи нет.

## Risks / Trade-offs

- [База может устареть при резкой смене погоды] → медленное обучение + `trim` ±5 компенсирует.
- [На старте после ребута база = sp_min, первый цикл разгоняется] → один цикл, затем база выучена.
