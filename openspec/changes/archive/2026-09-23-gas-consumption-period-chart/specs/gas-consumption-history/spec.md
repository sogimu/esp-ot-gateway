## ADDED Requirements

### Requirement: Hourly gas consumption accumulation

The system SHALL accumulate gas consumption (m³) into hourly buckets while the burner flame is active. On each hour boundary (local wall clock), the current hour's accumulated volume SHALL be archived into an hourly ring of 48 slots (today + yesterday), and the accumulator SHALL reset to zero for the new hour.

#### Scenario: Flow accumulates into the current hour bucket

- **WHEN** the burner is firing and wall clock is synced
- **THEN** the current hour's accumulator grows by `flow × dt` at each execution tick

#### Scenario: Hour boundary archives the completed hour

- **WHEN** the local hour changes between two execution ticks
- **THEN** the previous hour's accumulated volume is stored in the hourly ring keyed by its `epoch_hour`, and the accumulator resets to zero

#### Scenario: Clock jumps over multiple hours

- **WHEN** the local clock advances by more than one hour between ticks (e.g. NTP sync, timezone change)
- **THEN** skipped hours are stored as zero-volume entries in the hourly ring and the current hour resumes accumulating normally

### Requirement: Two-month daily consumption history

The system SHALL retain up to 64 completed days of daily gas consumption (m³) in a ring buffer, plus the running accumulation for today. The daily history SHALL NOT be reset by meter corrections.

#### Scenario: Day boundary archives the completed day

- **WHEN** the local day changes between two execution ticks
- **THEN** the previous day's total is stored in the daily ring keyed by its `epoch_day`, and today's accumulator resets to zero

#### Scenario: Ring retains the most recent 64 days

- **WHEN** more than 64 days are accumulated
- **THEN** the oldest completed days are dropped and the ring keeps the most recent 64 completed days plus today

#### Scenario: Meter correction does not erase history

- **WHEN** a meter correction resets the integral (`set_integral(0)`)
- **THEN** the daily and hourly history, and the running today/hourly accumulators, are unchanged

### Requirement: Wear-aware NVS persistence of gas history

The system SHALL persist gas history to NVS using a split strategy: a small "today + current hour" record saved frequently (on the regular persistence cadence) so a reboot loses no more than one persistence interval, and a larger "completed history" record saved once per day at the day boundary.

#### Scenario: Today's running data survives a reboot

- **WHEN** the device reboots mid-day
- **THEN** after startup the today accumulator and current hour accumulator are restored from NVS

#### Scenario: Completed history is restored after reboot

- **WHEN** the device reboots after one or more completed days
- **THEN** the daily ring and hourly ring are restored from the completed-history NVS record

#### Scenario: No NVS data yields an empty history

- **WHEN** NVS contains no gas history record (first boot)
- **THEN** the history starts empty without error
