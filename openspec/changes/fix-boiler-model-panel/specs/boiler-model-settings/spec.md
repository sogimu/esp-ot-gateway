## ADDED Requirements

### Requirement: Boiler model parameters are accepted by the control API

The system SHALL accept the boiler-model parameters `gas_temp_offset`, `ch_pmin`, `ch_pmax`, `dhw_pmin`, `dhw_pmax` in `POST /api/control` and apply them through the gas-calibration interactor, which validates and persists them. CH and DHW power ranges SHALL remain independent pairs. Fields absent from the request SHALL leave the corresponding stored value unchanged.

#### Scenario: CH power range is applied

- **WHEN** the request contains both `ch_pmin` and `ch_pmax`
- **THEN** the gas model uses the submitted values as the CH input-power range and they survive a reboot

#### Scenario: DHW power range is applied

- **WHEN** the request contains both `dhw_pmin` and `dhw_pmax`
- **THEN** the gas model uses the submitted values as the DHW input-power range and they survive a reboot

#### Scenario: Gas temperature offset is applied

- **WHEN** the request contains `gas_temp_offset`
- **THEN** the gas model uses the submitted offset for the calorific-value correction

#### Scenario: Absent fields are left unchanged

- **WHEN** the request contains only `gas_temp_offset`
- **THEN** the stored CH and DHW power ranges are not modified

### Requirement: Boiler model panel has an Apply action

The boiler-model panel SHALL provide a single action button that submits all its editable fields in one request and reports the outcome. The button and its feedback label SHALL follow the same visual pattern as the neighbouring control panels (`.btn` class plus a message span).

#### Scenario: Successful apply

- **WHEN** the user presses the apply button and the server accepts the request
- **THEN** the panel shows a success message

#### Scenario: Failed apply

- **WHEN** the user presses the apply button and the request fails or is rejected
- **THEN** the panel shows an error message

#### Scenario: Apply button is fully visible

- **WHEN** the user opens the boiler-model panel at the bottom of the longest tab
- **THEN** the apply button is fully visible and clickable, not clipped by the page layout

### Requirement: Model panel edits are protected from polling

While the boiler-model panel has unsaved edits, the periodic statistics poll SHALL NOT overwrite its input fields. An edit SHALL mark the panel dirty; the dirty state SHALL be cleared only after a successful apply.

#### Scenario: Polling does not overwrite an edited field

- **WHEN** the user edits a model field and a statistics poll completes before the user applies the changes
- **THEN** the field keeps the user-entered value

#### Scenario: Dirty state clears after a successful apply

- **WHEN** the user applies the model changes and the server accepts them
- **THEN** the dirty state is cleared and subsequent polls may refresh the fields from the stored values

### Requirement: Efficiency curve is not configurable in the model panel

The boiler-model panel SHALL NOT expose efficiency-curve inputs, and the apply request SHALL NOT include efficiency-curve fields. The efficiency curve does not participate in the gas-flow calculation.

#### Scenario: Panel has no efficiency inputs

- **WHEN** the user opens the boiler-model panel
- **THEN** no Т1/Т2/Т3 efficiency inputs are shown

#### Scenario: Apply request carries no efficiency fields

- **WHEN** the user presses the apply button
- **THEN** the request body contains only the gas temperature offset and the CH/DHW power ranges
