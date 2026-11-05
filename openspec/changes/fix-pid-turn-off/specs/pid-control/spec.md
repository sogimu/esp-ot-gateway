## ADDED Requirements

### Requirement: PID гасит отопление битом CH при достижении цели

Когда в `PidPollInteractor` гистерезис даёт `ch_on == false` (комната выше `target + hysteresis`), система SHALL сбросить `pid_ch_enabled` в `false`, не изменяя уставку CH. Уставка CH SHALL оставаться результатом расчёта PI.

#### Scenario: Комната нагрелась выше цели

- **WHEN** комната выше `target + hysteresis`
- **THEN** `pid_ch_enabled` равен `false`, а уставка CH остаётся расчётным выходом PID и не подменяется на `MIN_SETPOINT`

#### Scenario: Комната остыла ниже цели

- **WHEN** комната ниже `target - hysteresis`
- **THEN** `pid_ch_enabled` равен `true`, а уставка CH равна расчётному выходу PID

### Requirement: Master-байт гасит CH по решению PID

В адаптивном (PID) режиме `BoilerPollInteractor::build_master_byte()` SHALL сбрасывать бит `MASTER_CH_ENABLE`, когда `pid_ch_enabled == false`, даже если пользовательский `ch_enable == true`. В ручном режиме бит CH определяется только `ch_enable`.

#### Scenario: PID запретил нагрев

- **WHEN** режим PID активен и `pid_ch_enabled == false`
- **THEN** бит `MASTER_CH_ENABLE` не выставляется в master-байте

#### Scenario: Ручной режим

- **WHEN** режим ручной (`Manual_Static`/`Manual_Sched`)
- **THEN** бит `MASTER_CH_ENABLE` выставляется только по `ch_enable`
