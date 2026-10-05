## MODIFIED Requirements

### Requirement: Test tag cleanup after successful release

After the stable release is successfully created from a promoted tag, the pipeline SHALL delete every test release and test tag of the same semantic version — all `vX.Y.Z-rc*` siblings of the promoted stable `vX.Y.Z`. Test releases and test tags of other versions SHALL NOT be deleted. When promotion was skipped because the stable release already existed, no test tag SHALL be deleted.

#### Scenario: Cleanup after successful promotion

- **WHEN** the release run for the promoted stable tag creates the stable release
- **THEN** every `vX.Y.Z-rc*` release and tag of the promoted version is deleted

#### Scenario: Test tags of other versions are left intact

- **WHEN** the stable release is created and a test tag of a different version (e.g. `vX.Y+1.0-rc1`) exists
- **THEN** that test tag and its release are left untouched

#### Scenario: Skip leaves test tag intact

- **WHEN** promotion does not create a stable tag (no test tag or stable tag already exists)
- **THEN** all test tags and releases remain untouched
