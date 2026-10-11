# ch-heating-control

## Purpose

Корректное управление отоплением CH в ручном режиме: уставки обрезаются по границам, сообщённым котлом (единый источник истины), реализован гистерезис включения/выключения отопления, при рукопожатии в Master Configuration записывается собственная конфигурация мастера.

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

### Requirement: Master Configuration is written correctly on handshake

During handshake, the system SHALL write to Master Configuration (OpenTherm ID 2) the master's own flags and Member ID, and SHALL NOT echo the Slave Configuration (ID 3) value into ID 2. The slave configuration read from ID 3 SHALL be retained internally for control decisions (e.g. DHW presence, storage tank) but not written back to ID 2.

#### Scenario: Master config carries master identity

- **WHEN** handshake completes
- **THEN** ID 2 contains the master's own Member ID and configuration flags, not the boiler's

#### Scenario: Slave config is not echoed to master config

- **WHEN** handshake reads Slave Configuration (ID 3)
- **THEN** the raw ID 3 value is not written to ID 2; the slave's flags are stored separately for control logic

#### Scenario: Smart Power flag reflects master capability

- **WHEN** the master does not support Smart Power
- **THEN** the Smart Power bit in ID 2 is clear, regardless of the boiler's DHW-present flag
