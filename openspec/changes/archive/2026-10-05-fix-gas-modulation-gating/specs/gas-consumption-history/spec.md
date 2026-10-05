## ADDED Requirements

### Requirement: Firing is determined by modulation, not the flame bit

The system SHALL treat the burner as firing only while the OpenTherm relative modulation level is above zero. The flame status bit SHALL NOT be used as the firing signal for gas accumulation. While the modulation level is zero, the estimated flow SHALL be zero and no gas volume SHALL be accumulated.

#### Scenario: Modulation above zero accumulates flow

- **WHEN** the relative modulation level is above zero
- **THEN** the flow rate is computed from the modulation and the accumulated volume grows by `flow × dt`

#### Scenario: Zero modulation produces no flow regardless of flame

- **WHEN** the relative modulation level is zero, even while the flame status bit is set
- **THEN** the flow rate is zero and no volume is accumulated

## MODIFIED Requirements

### Requirement: Hourly gas consumption accumulation

The system SHALL accumulate gas consumption (m³) into hourly buckets while the burner is firing (relative modulation level above zero). On each hour boundary (local wall clock), the current hour's accumulated volume SHALL be archived into an hourly ring of 48 slots (today + yesterday), and the accumulator SHALL reset to zero for the new hour.

#### Scenario: Flow accumulates into the current hour bucket

- **WHEN** the burner is firing (relative modulation level above zero) and the wall clock is synced
- **THEN** the current hour's accumulator grows by `flow × dt` at each execution tick

#### Scenario: Hour boundary archives the completed hour

- **WHEN** the local hour changes between two execution ticks
- **THEN** the previous hour's accumulated volume is stored in the hourly ring keyed by its `epoch_hour`, and the accumulator resets to zero

#### Scenario: Clock jumps over multiple hours

- **WHEN** the local clock advances by more than one hour between ticks (e.g. NTP sync, timezone change)
- **THEN** skipped hours are stored as zero-volume entries in the hourly ring and the current hour resumes accumulating normally
