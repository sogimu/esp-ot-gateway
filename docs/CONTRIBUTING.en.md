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

## Contribution license

The project is released under GPL-3.0 (see [LICENSING.en.md](LICENSING.en.md)). Contributions are accepted on an inbound=outbound basis: by submitting a contribution (code, documentation or other material) you agree that it is distributed under the same GPL-3.0 license as the project, and you certify that you have the right to submit it under those terms (you wrote it yourself or otherwise hold the necessary rights, and it does not knowingly infringe third-party rights). You retain your own copyright.

Optionally, you may sign off your commits with the Developer Certificate of Origin (`git commit -s`, which adds a `Signed-off-by` line) to certify your right to contribute.

If you cannot agree to these terms (for example, your employer owns your work), please say so in the PR and we'll figure out an alternative.
