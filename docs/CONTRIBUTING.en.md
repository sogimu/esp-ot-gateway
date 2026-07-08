# Contributing

**English** | [Русский](CONTRIBUTING.md)

Thanks for helping make more boilers smart! Bug reports, boiler-compatibility reports and pull requests are all welcome.

## Reporting

- **Bugs:** attach the event journal around the incident (Log tab), the decoded backtrace if the device crashed (see [docs/build.en.md](build.en.md)), and your boiler model.
- **New boiler compatibility:** open an issue titled `Compatibility: <brand> <model>` with what worked, what didn't, and OT frames from the log if possible.

## Pull requests

1. Discuss non-trivial changes in an issue first.
2. Keep `domain/` free of ESP-IDF dependencies; add unit tests for new logic.
3. The host test suite and sanitizers must pass (see [docs/build.en.md](build.en.md)).

## Contributor License Grant

*(Template — have it reviewed by a lawyer before relying on it commercially.)*

By submitting a contribution (code, documentation or other material) to this repository you:

1. **Certify** that you wrote the contribution yourself or otherwise have the right to submit it under these terms, and that it does not knowingly infringe third-party rights.
2. **Grant** the project maintainer a perpetual, worldwide, non-exclusive, irrevocable, royalty-free right to use, reproduce, modify, distribute, publicly display, sublicense and **relicense** your contribution — including under commercial or proprietary license terms — as part of this project or derivative works.
3. **Retain** your own copyright: you may reuse your contribution anywhere else without restriction.
4. Understand that the project's public releases are currently licensed under the PolyForm Noncommercial License 1.0.0 (see [LICENSING.en.md](LICENSING.en.md)), and that the maintainer may offer the project, including your contribution, under separate commercial licenses.

If you cannot agree to this grant (for example, your employer owns your work), please say so in the PR and we'll figure out an alternative.
