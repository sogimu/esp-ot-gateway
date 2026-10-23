# temperature-correction Specification

## Purpose
Пользовательские аддитивные сдвиги (±0.1 °C) для температур T1/T2/уличной, применяемые через единую абстракцию чтения `ITemperatureSource`.
## Requirements
### Requirement: Temperature offsets are accepted by the control API

The system SHALL accept the offset fields `t1_offset`, `t2_offset`, `outside_offset` in `POST /api/control`, clamp each to `[-10.0, +10.0]` °C, round each to `0.1` °C, and persist them in NVS so they survive a reboot. Fields absent from the request SHALL leave the corresponding stored offset unchanged.

#### Scenario: Offset is applied and persisted

- **WHEN** the request contains `t1_offset: 1.5`
- **THEN** the stored `t1` offset becomes `1.5` and survives a reboot

#### Scenario: Offset is clamped to bounds

- **WHEN** the request contains `t2_offset: 25.0`
- **THEN** the stored `t2` offset is clamped to `10.0`

#### Scenario: Offset is rounded to 0.1 °C

- **WHEN** the request contains `t1_offset: 1.23`
- **THEN** the stored `t1` offset becomes `1.2`

#### Scenario: Absent fields are left unchanged

- **WHEN** the request contains only `outside_offset`
- **THEN** the stored offsets for the other two temperatures are not modified

### Requirement: Corrected temperature is read only through an abstraction

All application logic SHALL obtain temperatures through `ITemperatureSource`, which returns `raw + offset` for T1/T2/outside and the raw value for CH supply/CH return/DHW (these three are not user-correctable). Direct reads of raw temperature getters (`get_t1_temp`, `get_t2_temp`, `get_ch_temp`, `get_return_temp`, `get_dhw_temp`, `get_outside_temp`) from `IHeatingStateStore` SHALL NOT be used by application logic (they remain available only for the writers and the source adapter).

#### Scenario: Offset shifts the corrected reading

- **WHEN** the raw `t1` reading is `21.0` and `t1_offset` is `-1.5`
- **THEN** `ITemperatureSource::t1()` returns `19.5`

#### Scenario: Zero offset returns the raw reading

- **WHEN** an offset is `0`
- **THEN** the corrected reading equals the raw reading

#### Scenario: CH/DHW temperatures are not corrected

- **WHEN** CH supply, CH return or DHW temperature is read through `ITemperatureSource`
- **THEN** the returned value equals the raw boiler reading (no offset is applied)

#### Scenario: Consumers read corrected values

- **WHEN** the PID room sensor and the gas model need a temperature
- **THEN** they obtain it from `ITemperatureSource`, not from the raw store getters

### Requirement: Correction panel has an Apply action

The temperature-correction panel SHALL be located in the Control ("Управление") tab, before the boiler shutdown button, and SHALL provide a single Apply button that submits all three offsets in one request and reports the outcome, following the same visual pattern as the boiler-model panel (`.btn` class plus a message span).

#### Scenario: Successful apply

- **WHEN** the user presses the apply button and the server accepts the request
- **THEN** the panel shows a success message and clears the dirty state

#### Scenario: Failed apply

- **WHEN** the user presses the apply button and the request fails or is rejected
- **THEN** the panel shows an error message

### Requirement: Correction fields are uniform and protected from polling

The correction input fields SHALL use the same inline number-input styling as the boiler-model fields (`mf_*`) and SHALL support a per-panel dirty flag. While the panel has unsaved edits, the periodic statistics poll SHALL NOT overwrite the fields; the dirty state SHALL be cleared only after a successful apply.

#### Scenario: Polling does not overwrite an edited field

- **WHEN** the user edits an offset field and a statistics poll completes before the user applies the changes
- **THEN** the field keeps the user-entered value

#### Scenario: Dirty state clears after a successful apply

- **WHEN** the user applies the offset changes and the server accepts them
- **THEN** the dirty state is cleared and subsequent polls may refresh the fields from the stored values

