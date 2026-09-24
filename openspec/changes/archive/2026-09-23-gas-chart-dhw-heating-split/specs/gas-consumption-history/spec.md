## ADDED Requirements

### Requirement: DHW/heating consumption split

The system SHALL accumulate gas consumption split by operating mode alongside the total: while the burner is firing, each tick's volume SHALL be added to the total accumulator and, when the DHW 3-way valve is active (`dhw_active`), also to a dedicated DHW accumulator. Each stored bucket (hour and day) SHALL carry both the total volume (`m3_total`) and the DHW volume (`m3_dhw`); the heating share is the difference `m3_total − m3_dhw`.

#### Scenario: Flow is attributed to DHW while DHW is active

- **WHEN** the burner fires and `dhw_active` is true
- **THEN** the tick's volume is added to both the total accumulator and the DHW accumulator

#### Scenario: Flow is attributed to heating otherwise

- **WHEN** the burner fires and `dhw_active` is false
- **THEN** the tick's volume is added to the total accumulator only, so the heating share equals `m3_total − m3_dhw`

#### Scenario: DHW split is archived at bucket boundaries

- **WHEN** an hour or day boundary archives the completed bucket
- **THEN** the archived bucket stores both `m3_total` and `m3_dhw`

#### Scenario: DHW split survives a reboot

- **WHEN** the device reboots after accumulating a DHW/heating split
- **THEN** the today record and completed-history record restore both `m3_total` and `m3_dhw` from NVS

#### Scenario: Reset clears the DHW split

- **WHEN** gas statistics are reset
- **THEN** both the total and DHW accumulators for today, current hour, and daily history are cleared
