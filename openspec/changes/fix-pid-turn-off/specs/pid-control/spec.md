## ADDED Requirements

### Requirement: PID гасит отопление при достижении цели

Когда в `PidPollInteractor` гистерезис даёт `ch_on == false` (комната выше `target + hysteresis`), система SHALL опустить уставку CH до `MIN_SETPOINT` (25 °C) и сбросить `pid_ch_enabled` в `false`.

#### Scenario: Комната нагрелась выше цели

- **WHEN** комната выше `target + hysteresis`
- **THEN** уставка CH в состоянии равна `MIN_SETPOINT` (25 °C), а `pid_ch_enabled` равен `false`

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
