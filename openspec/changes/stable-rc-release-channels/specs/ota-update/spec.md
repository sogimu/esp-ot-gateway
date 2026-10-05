## ADDED Requirements

### Requirement: Version catalog endpoint

The device SHALL expose `GET /api/ota/versions` returning the version catalog fetched from GitHub Pages. On a fetch or transport failure the endpoint SHALL return an empty list with an error indication and SHALL NOT crash or reboot the device.

#### Scenario: Catalog available

- **WHEN** `GET /api/ota/versions` is called and the catalog is reachable
- **THEN** the response is `200` with the catalog JSON body

#### Scenario: Catalog unreachable

- **WHEN** `GET /api/ota/versions` is called and the catalog cannot be fetched
- **THEN** the response contains `"versions":[]` with an error field and the device keeps running

### Requirement: Checksum verification on OTA upload

When the catalog contains a `sha256` for the selected tag, the device SHALL verify the SHA-256 of the uploaded image and SHALL reject the update on mismatch. When no checksum is present for the tag, the device SHALL accept the image as before.

#### Scenario: Matching checksum accepts update

- **WHEN** the browser uploads an image whose SHA-256 equals the catalog `sha256` for the tag
- **THEN** the device completes the OTA write and proceeds as before

#### Scenario: Mismatched checksum rejects update

- **WHEN** the browser uploads an image whose SHA-256 differs from the catalog `sha256` for the tag
- **THEN** the device aborts the OTA write, reports an error, and does not boot the image

#### Scenario: No checksum for tag

- **WHEN** the selected tag has no `sha256` in the catalog
- **THEN** the device performs the update without checksum verification

### Requirement: Test versions are selectable on existing devices

The device OTA version list SHALL include test versions so that already-deployed devices can install them for testing. Test versions SHALL be identifiable by the `-rcN` tag suffix and SHALL be ordered after stable versions.

#### Scenario: Legacy device lists test versions

- **WHEN** an already-deployed device renders the catalog without channel awareness
- **THEN** test versions appear in the list after stable versions, labeled by their tag containing `-rcN`

### Requirement: Test versions are grouped and opt-in on new firmware

New device firmware SHALL present test (prerelease) versions in a separate «Тестовые» group under the stable versions. A test version SHALL NOT be auto-selected, and starting an update with a test version selected SHALL require explicit user confirmation.

#### Scenario: Test group is separate

- **WHEN** the OTA version list contains both stable and test versions
- **THEN** stable versions are shown in the default group and test versions in a distinct «Тестовые» group

#### Scenario: Confirmation required for test update

- **WHEN** the user selects a test version and starts the update
- **THEN** the device asks for explicit confirmation before downloading and flashing the test image
