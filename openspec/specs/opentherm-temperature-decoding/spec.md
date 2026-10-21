# opentherm-temperature-decoding Specification

## Purpose
TBD - created by archiving change fix-f88-signed-temperature. Update Purpose after archive.
## Requirements
### Requirement: Signed f8.8 temperature values decode correctly

The system SHALL decode OpenTherm f8.8 temperature values (e.g. outside temperature, CH supply, CH return) as signed two's-complement, so negative temperatures yield negative `float` degrees Celsius instead of large positive values near 254–255.

#### Scenario: Negative outside temperature decodes correctly

- **WHEN** the boiler returns raw value `0xFE00` (-2.0°C) for the outside-temperature data ID
- **THEN** the decoded value is `-2.0°C`, not `254.0°C`

#### Scenario: Zero decodes to zero

- **WHEN** the boiler returns raw value `0x0000`
- **THEN** the decoded value is `0.0°C`

#### Scenario: Negative fractional temperature decodes correctly

- **WHEN** the boiler returns raw value `0xFF80` (-0.5°C)
- **THEN** the decoded value is `-0.5°C`

#### Scenario: Extreme negative temperature decodes correctly

- **WHEN** the boiler returns raw value `0xCE00` (-50°C)
- **THEN** the decoded value is `-50.0°C`

#### Scenario: Positive temperature decodes unchanged

- **WHEN** the boiler returns raw value `0x1400` (+20.0°C)
- **THEN** the decoded value is `20.0°C`

### Requirement: Unsigned f8.8 values are not reinterpreted

Values that are not signed temperatures (relative modulation %, version, fault/status codes, byte-pair bounds) SHALL keep their existing unsigned interpretation and SHALL NOT be routed through the signed temperature decoder.

#### Scenario: Modulation stays unsigned

- **WHEN** the boiler returns a modulation raw value with the high byte set (e.g. `0x8000` = 128%)
- **THEN** the value is interpreted as an unsigned percentage, not a negative temperature

