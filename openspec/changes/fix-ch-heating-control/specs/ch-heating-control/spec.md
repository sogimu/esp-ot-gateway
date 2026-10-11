# ch-heating-control

## Purpose

Корректное управление отоплением CH в ручном режиме: уставки обрезаются по границам, сообщённым котлом (единый источник истины), реализован гистерезис включения/выключения отопления. Master Configuration (ID 2) сохраняет эхо slave-конфигурации — это эмпирический workaround для Baxi.

## ADDED Requirements

### Requirement: Setpoint bounds are a single source of truth

The system SHALL clamp CH and DHW setpoint values to the bounds reported by the boiler (OpenTherm ID 49 for CH, ID 48 for DHW) in a single, shared place, and SHALL apply this clamping consistently across all entry points (HTTP, MQTT, schedule, use case). Clamping SHALL never widen the boiler-reported range, and SHALL fall back to a safe default range only until the bounds are first read from the boiler.

#### Scenario: Setpoint below boiler minimum is clamped

- **WHEN** a client submits a CH setpoint below the boiler-reported lower bound via any entry point
- **THEN** the stored and written setpoint is the boiler-reported lower bound

#### Scenario: Setpoint above boiler maximum is clamped

- **WHEN** a client submits a CH setpoint above the boiler-reported upper bound
- **THEN** the stored and written setpoint is the boiler-reported upper bound

#### Scenario: All entry points clamp identically

- **WHEN** the same out-of-range setpoint is submitted via HTTP, MQTT, and the schedule
- **THEN** all three produce the same clamped value, derived from the shared bounds

#### Scenario: Bounds not yet read from boiler

- **WHEN** the setpoint is set before the boiler bounds have been read
- **THEN** the system clamps to a safe default range and re-clamps once the boiler reports its bounds

### Requirement: CH hysteresis controls heating on and off

In manual mode, the system SHALL implement an on/off control loop on the CH flow temperature using a configurable hysteresis: when the flow temperature falls below `ch_setpoint - hysteresis`, the system SHALL enable CH (write `CH_ENABLE=1`); when the flow temperature reaches `ch_setpoint`, the system SHALL disable CH (write `CH_ENABLE=0`), allowing the boiler to stop the burner and run pump overrun.

#### Scenario: Heating starts below the band

- **WHEN** CH is enabled, manual mode is active, and the flow temperature drops below `ch_setpoint - hysteresis`
- **THEN** the gateway writes `CH_ENABLE=1` in the OpenTherm STATUS frame

#### Scenario: Heating stops at the setpoint

- **WHEN** CH is enabled and the flow temperature reaches or exceeds `ch_setpoint`
- **THEN** the gateway writes `CH_ENABLE=0`, letting the boiler stop the burner and run pump overrun

#### Scenario: Hysteresis keeps state in the dead band

- **WHEN** the flow temperature is between `ch_setpoint - hysteresis` and `ch_setpoint`
- **THEN** the gateway keeps the current `CH_ENABLE` value unchanged

#### Scenario: Disabling CH overrides hysteresis

- **WHEN** the user disables CH (`ch_enable=false`)
- **THEN** the hysteresis loop SHALL NOT re-enable CH until the user re-enables it

### Requirement: Master Configuration echoes slave configuration as a Baxi workaround

During handshake, the system SHALL write the Slave Configuration (ID 3) value verbatim into Master Configuration (ID 2). This is an empirical workaround required by the Baxi Duo-tec Compact to allow DHW control (three-way valve switching to the cylinder); writing the master's own config instead breaks DHW on this boiler.

#### Scenario: Slave config is echoed to master config

- **WHEN** handshake reads Slave Configuration (ID 3)
- **THEN** the raw ID 3 value is written to ID 2

#### Scenario: DHW control remains functional

- **WHEN** the echo workaround is in place and DHW is enabled
- **THEN** the boiler permits DHW control and switches the three-way valve to the cylinder when DHW priority is active
