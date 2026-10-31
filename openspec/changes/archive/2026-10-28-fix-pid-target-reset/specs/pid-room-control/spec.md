## ADDED Requirements

### Requirement: The PID room target persists across CH mode switches
The system SHALL preserve the configured PID room target when the CH mode changes between Manual and Adaptive and when the PID is disabled.

#### Scenario: Adaptive → Manual → Adaptive
- **WHEN** the room target is 22.5 °C, the user switches from the Adaptive mode to Manual and back to Adaptive
- **THEN** the room target remains 22.5 °C and the UI shows 22.5 °C

#### Scenario: Disabled PID poll keeps the target
- **WHEN** the PID is disabled and the poll task executes
- **THEN** the configured room target is unchanged

### Requirement: The last measured room temperature is preserved while the PID is disabled
The system SHALL not zero the last measured room temperature when the PID is disabled.

#### Scenario: Disable preserves the room temperature
- **WHEN** the PID is disabled after measuring 21.8 °C
- **THEN** the last measured room temperature remains 21.8 °C
