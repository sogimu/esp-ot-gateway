## ADDED Requirements

### Requirement: Version list ordering

The web flasher SHALL sort the release list by semantic version in descending order, with stable versions before test (prerelease) versions. Ordering SHALL NOT depend on the order returned by the GitHub API.

#### Scenario: Stable descending

- **WHEN** the flasher loads releases v0.7.1 and v0.8.0 (both stable)
- **THEN** v0.8.0 is listed before v0.7.1

#### Scenario: Test versions after stable

- **WHEN** the flasher loads a mix of stable and test releases
- **THEN** all stable versions are listed before all test versions

### Requirement: Default version selection

The flasher SHALL auto-select the highest stable version by semantic version. It SHALL NOT auto-select a test version and SHALL NOT rely on the API order or publication date.

#### Scenario: Highest stable is default

- **WHEN** the flasher loads v0.8.0 and v0.7.1 (both stable) and v0.9.0-rc1 (test)
- **THEN** v0.8.0 is selected by default

#### Scenario: Patch published later does not win

- **WHEN** v0.7.1 is published after v0.8.0
- **THEN** v0.8.0 is still the default

### Requirement: Test versions section

The flasher SHALL present test versions in a separate visual section or option group, visually distinct from stable versions. Test versions SHALL never be auto-selected.

#### Scenario: Test section rendering

- **WHEN** the release list contains test versions
- **THEN** they are rendered in a distinct section/group from stable versions

### Requirement: Deep-link to a specific version

The flasher SHALL accept a `tag` query parameter (e.g. `flasher.html?tag=v0.9.0-rc1`) and preselect that version, including test versions, disabling automatic default selection. If the requested tag is not present, the flasher SHALL fall back to the default stable selection.

#### Scenario: Deep-link to test version

- **WHEN** the page is opened with `?tag=v0.9.0-rc1` and that release exists
- **THEN** v0.9.0-rc1 is selected and flashing uses its manifest

#### Scenario: Unknown tag falls back

- **WHEN** the page is opened with `?tag=vNope` that does not match any release
- **THEN** the flasher selects the highest stable version instead

### Requirement: Manifest availability fallback

Before enabling flashing for a selected version, the flasher SHALL verify that `firmware/<tag>/manifest.json` is available. On a missing manifest the flasher SHALL show an explanatory message and, when the selection was automatic, fall back to the next stable version whose manifest exists.

#### Scenario: Manifest missing for default

- **WHEN** the default version's manifest returns 404
- **THEN** the flasher falls back to the next lower stable version with an available manifest and informs the user

#### Scenario: Manifest missing for explicit choice

- **WHEN** the user explicitly selects a version whose manifest returns 404
- **THEN** the flasher shows an error explaining that the build is not published on Pages and does not enable flashing

#### Scenario: Manifest available

- **WHEN** the selected version's manifest is reachable
- **THEN** flashing is enabled for that version
