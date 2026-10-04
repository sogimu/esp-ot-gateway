## ADDED Requirements

### Requirement: Stable tag reachability guard

The release pipeline SHALL reject a stable tag (`vMAJOR.MINOR.PATCH`) whose commit is not an ancestor of `master`. The guard SHALL NOT apply to test tags (`vMAJOR.MINOR.PATCH-rcN`).

#### Scenario: Stable tag on merged commit

- **WHEN** a stable tag `vX.Y.Z` is pushed and its commit is reachable from `master`
- **THEN** the release job proceeds and a GitHub Release is created

#### Scenario: Stable tag on unmerged commit

- **WHEN** a stable tag `vX.Y.Z` is pushed and its commit is not reachable from `master`
- **THEN** the release job fails and no GitHub Release is created

#### Scenario: Test tag on feature branch

- **WHEN** a test tag `vX.Y.Z-rcN` is pushed from a feature branch not merged into `master`
- **THEN** the reachability guard does not block the pipeline and the release job proceeds

### Requirement: Release channel classification

The pipeline SHALL classify tags into exactly two channels: `vX.Y.Z` is stable and SHALL be published as a latest (non-prerelease) release; `vX.Y.Z-rcN` is test and SHALL be published as a prerelease. No other version suffix SHALL be treated as a test channel.

#### Scenario: Stable release flags

- **WHEN** a stable tag is published
- **THEN** the GitHub Release is created as non-prerelease and marked latest

#### Scenario: Test release flags

- **WHEN** a test tag `vX.Y.Z-rcN` is published
- **THEN** the GitHub Release is created with the prerelease flag and is not marked latest

### Requirement: Release notes from tag annotation

The release body SHALL be taken from the annotated tag message via `--notes-from-tag` when the annotation is non-empty. Auto-generated notes SHALL be used only as a fallback when the tag annotation is empty.

#### Scenario: Annotated tag provides notes

- **WHEN** a release is created for a tag with a non-empty annotation
- **THEN** the release body equals the tag annotation

#### Scenario: Empty annotation falls back

- **WHEN** a release is created for a tag whose annotation is empty
- **THEN** the release body is auto-generated from merged pull requests

#### Scenario: Existing release is not rewritten

- **WHEN** the workflow runs again for a tag whose release already exists
- **THEN** only release assets are uploaded and the release notes are left unchanged

### Requirement: Pages deployment on release tags

The Pages deployment SHALL run on pushes of `v*` tags in addition to pushes to `master`. On a tag run it SHALL publish the firmware built by the same workflow run from its build artifact, without depending on the release or its assets. The whole site SHALL be reassembled because GitHub Pages deployment is atomic.

#### Scenario: Firmware published on tag

- **WHEN** a `v*` tag is pushed
- **THEN** `site/firmware/<tag>/` contains the binaries built in that run plus a `manifest.json`, and `versions.json` is regenerated

#### Scenario: No race with release creation

- **WHEN** the Pages deployment for a tag runs
- **THEN** the firmware for the pushed tag is taken from the build artifact of the current run and not downloaded from the GitHub Release

#### Scenario: Master push still deploys

- **WHEN** a commit is pushed to `master`
- **THEN** the Pages deployment runs and publishes the full site as before

#### Scenario: Site root comes from master on a tag run

- **WHEN** the Pages deployment runs for a tag
- **THEN** the web flasher published at the site root comes from `master` (not from the tagged commit), while the firmware for the pushed tag comes from the build artifact

### Requirement: Catalog checksum emission

The pipeline SHALL compute the SHA-256 of the exact `esp-ot-gateway.bin` that is published to Pages and SHALL include it in `versions.json` for the corresponding version.

#### Scenario: Hash matches distributed binary

- **WHEN** `versions.json` is generated
- **THEN** the `sha256` value of each version equals the SHA-256 of the `esp-ot-gateway.bin` available at `firmware/<tag>/esp-ot-gateway.bin`

#### Scenario: Tag run hashes the artifact

- **WHEN** `versions.json` is generated during a tag run
- **THEN** the `sha256` for the pushed tag is computed from the build artifact of that run

### Requirement: Pull requests publish test tags only

A pull request branch SHALL produce only test (`vX.Y.Z-rcN`) releases. A stable release SHALL NOT be creatable from a commit not reachable from `master`; the release job fails for such a stable tag and no Release is published.

#### Scenario: Test tag from a pull request

- **WHEN** a `vX.Y.Z-rcN` tag is pushed from a pull request branch
- **THEN** a prerelease with firmware assets and a Pages deployment is produced

#### Scenario: Stable tag from a pull request

- **WHEN** a stable `vX.Y.Z` tag is pushed from a pull request branch before merge
- **THEN** the release job fails and no stable Release is published

### Requirement: Automatic stable promotion on merge

When a pull request is merged into `master`, the pipeline SHALL create a stable tag derived from the highest test tag reachable from the pull request head, pointing at the pull request's merge commit. It SHALL do nothing when the pull request has no test tag, and SHALL NOT overwrite an existing stable tag or release.

#### Scenario: Merged pull request with a test tag

- **WHEN** a PR with `vX.Y.Z-rcN` reachable from its head is merged into `master`
- **THEN** an annotated stable tag `vX.Y.Z` is created on the PR merge commit and the release pipeline is started for it

#### Scenario: Multiple test tags

- **WHEN** several test tags are reachable from the PR head
- **THEN** the highest one by semantic version is promoted

#### Scenario: No test tag

- **WHEN** a merged PR has no test tag
- **THEN** no stable tag or release is created automatically

#### Scenario: Stable tag already exists

- **WHEN** the derived stable tag `vX.Y.Z` already exists
- **THEN** promotion does not modify the existing tag or release

#### Scenario: Promoted version is not the highest

- **WHEN** the derived stable version is not higher than the highest existing stable tag (e.g. a stale `v0.8.0-rc1` while `v0.9.0` is out)
- **THEN** promotion still proceeds but emits a warning that the release may be unintentional, so legitimate patch releases are not blocked

### Requirement: Promotion starts the release run without secrets

Because a tag pushed with `GITHUB_TOKEN` does not trigger `push` workflows, the promotion SHALL start the release pipeline via `workflow_dispatch` at the newly created stable tag.

#### Scenario: Dispatch at the stable tag

- **WHEN** the promotion has created and pushed `vX.Y.Z`
- **THEN** a workflow dispatch run is started with ref `refs/tags/vX.Y.Z` and executes the release and Pages jobs

### Requirement: Release notes copied from the promoted test tag

The stable tag and its release body SHALL use the promoted test tag's annotation. When that annotation is empty, auto-generated notes SHALL be used instead.

#### Scenario: Annotation copied

- **WHEN** `vX.Y.Z-rcN` has a non-empty annotation
- **THEN** the created stable tag carries the same annotation and the release body equals it

#### Scenario: Empty annotation

- **WHEN** `vX.Y.Z-rcN` has no annotation
- **THEN** the stable release body is auto-generated

### Requirement: Test tag cleanup after successful release

After the stable release is successfully created from a promoted tag, the pipeline SHALL delete the originating test release and test tag. When promotion was skipped because the stable release already existed, the test tag SHALL NOT be deleted.

#### Scenario: Cleanup after successful promotion

- **WHEN** the release run for the promoted stable tag creates the stable release
- **THEN** the `vX.Y.Z-rcN` release and tag are deleted

#### Scenario: Skip leaves test tag intact

- **WHEN** promotion does not create a stable tag (no test tag or stable tag already exists)
- **THEN** the test tag and release remain untouched
