# setpoint-bounds

## Purpose

Уставки CH и DHW обрезаются по границам, сообщённым котлом (OpenTherm ID 49 для CH, ID 48 для DHW), в одном месте — единый источник истины для всех точек входа.

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

### Requirement: The written CH setpoint is re-clamped to the boiler maximum

The poll task SHALL re-clamp the CH setpoint against the boiler-reported upper bound (ID 49) before writing it to the boiler (ID 1), mirroring the existing DHW behavior, so that a value loaded from NVS or written by another path cannot exceed the boiler's reported maximum.

#### Scenario: Stored setpoint above the boiler maximum

- **WHEN** the stored CH setpoint exceeds the boiler-reported maximum and the poll task writes it
- **THEN** the written value is the boiler-reported maximum
