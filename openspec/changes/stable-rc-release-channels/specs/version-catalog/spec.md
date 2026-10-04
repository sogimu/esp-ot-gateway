## ADDED Requirements

### Requirement: Catalog document structure

`versions.json` SHALL be a JSON document with a top-level `versions` array. Each entry SHALL contain `tag`, `name`, `published_at`, `prerelease`, and `sha256`. The `tag` field SHALL appear before `sha256` within each entry.

#### Scenario: Catalog entry fields

- **WHEN** `versions.json` is fetched
- **THEN** every element of `versions` has `tag` (string), `name` (string), `published_at` (ISO 8601 string), `prerelease` (boolean), and `sha256` (64 lowercase hex characters)

#### Scenario: Version without a built binary

- **WHEN** a release has no downloadable firmware assets
- **THEN** the entry is absent from `versions.json` (entries without `sha256` are never emitted, because the deployed substring lookup cannot distinguish a missing hash from the next entry's hash)

### Requirement: Version ordering

The catalog SHALL list stable versions first, sorted by semantic version in descending order, followed by test (prerelease) versions, also sorted by semantic version in descending order. Ordering SHALL NOT depend on publication date.

#### Scenario: Stable versions first

- **WHEN** the catalog contains both stable and test versions
- **THEN** all `prerelease:false` entries precede all `prerelease:true` entries

#### Scenario: Semver order within a group

- **WHEN** the catalog contains v0.8.0 and v0.7.1 (stable) and v0.9.0-rc1 and v0.8.0-rc2 (test)
- **THEN** the stable group is ordered v0.8.0, v0.7.1 and the test group is ordered v0.9.0-rc1, v0.8.0-rc2

#### Scenario: Later-published patch does not win

- **WHEN** v0.7.1 is published after v0.8.0
- **THEN** v0.8.0 still appears before v0.7.1

### Requirement: Checksum in catalog

Each catalog entry with a distributed binary SHALL carry the SHA-256 of its `esp-ot-gateway.bin` so that consumers can verify image integrity.

#### Scenario: Checksum is the binary hash

- **WHEN** a consumer computes the SHA-256 of `firmware/<tag>/esp-ot-gateway.bin`
- **THEN** it equals the `sha256` field of the entry with the same `tag`

### Requirement: Legacy device compatibility

The catalog SHALL remain consumable by already-deployed firmware that performs substring lookup for `"tag":"<tag>"` followed by `"sha256":"`. The catalog body SHALL stay within the 64 KiB limit enforced by the device.

#### Scenario: Legacy device finds checksum

- **WHEN** deployed firmware looks up `"tag":"v0.8.0"` and then the next `"sha256":"` in the opaque catalog body
- **THEN** it finds the checksum belonging to that version

#### Scenario: Catalog fits device limit

- **WHEN** the catalog is generated for the current set of releases
- **THEN** its size is below 64 KiB

#### Scenario: Catalog is compact

- **WHEN** `versions.json` is generated
- **THEN** it contains no whitespace between JSON separators (e.g. `"tag":"v0.8.0"`, `"sha256":"…"`), so the substring lookups performed by already-deployed firmware match

### Requirement: Test versions remain listed

The catalog SHALL include test (prerelease) versions, marked with `prerelease:true` and ordered last, so that existing devices can still select them for testing. The catalog SHALL NOT be filtered to stable versions only.

#### Scenario: Test version is selectable

- **WHEN** a test version `vX.Y.Z-rcN` exists as a release
- **THEN** it appears in the catalog with `prerelease:true` and remains selectable by a device
