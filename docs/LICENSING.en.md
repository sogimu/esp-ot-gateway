# Licensing model

**English** | [Русский](LICENSING.md)

*(This document explains the licensing strategy. It is not legal advice; the license text itself is authoritative.)*

## Summary

- **Source code:** [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/) — free for any noncommercial purpose.
- **Personal / home use is noncommercial.** Flashing the firmware onto your own ESP32 and heating your own house: free, forever, including modifications and sharing.
- **Commercial use requires a paid license.** Examples: selling preflashed devices or kits, bundling the firmware with hardware, paid installation/integration services built on it, use inside a company's product or infrastructure. Contact the author (GitHub issue or profile email) for terms.
- **Contributions** are accepted under the license grant in [CONTRIBUTING.en.md](CONTRIBUTING.en.md), which allows the project to sublicense and relicense contributed code (this is what makes commercial licensing legally possible).

## Why PolyForm Noncommercial?

1. **The audience gets what it needs.** DIY users — the people this project is for — can use, study, patch and share it freely. Nothing changes for them.
2. **The author keeps commercial rights.** Under MIT/Apache/GPL, anyone may commercialize the code (GPL only forces them to share source). PolyForm NC reserves commercial exploitation to the copyright holder, so a hardware seller can't ship this firmware in a paid product without a deal.
3. **It's a real, lawyer-drafted, widely used license** — not a homemade "free for personal use" note, which tends to be ambiguous and hard to enforce.
4. **It works internationally.** Copyright is protected worldwide by the Berne Convention; the license is a plain-language, jurisdiction-neutral grant. In the EU and most of the world this is an ordinary copyright license. In Russia it fits the открытая лицензия framework (Art. 1286.1 of the Civil Code) as a publicly offered license with a noncommercial scope.

## FAQ

**Is this "open source"?**
Strictly speaking, no — OSI-approved open source may not restrict commercial use. This is *source-available*: the code is public, auditable and free for noncommercial use. We say "source code is open/available" rather than "open source" to be precise.

**Can I fork it and publish my fork?**
Yes, for noncommercial purposes, keeping the same license and required notices.

**Can I use it at work / in my company?**
Not under the noncommercial license — that needs a commercial license. A single developer experimenting at home is fine; a company heating its office or shipping products is not.

**I sell/install heating equipment and want to offer this to customers.**
That's exactly the commercial case — get in touch, terms are meant to be reasonable.

**What about versions released before this license?**
Any version previously published under a different license remains available under that license. This model applies from the commit that introduces the `LICENSE` file onward.

**Will the project stay source-available?**
Current intent: yes for the core. The author reserves the right to offer future advanced features under commercial terms only.

## How to apply (maintainer checklist)

1. Download the **unmodified** official text of PolyForm Noncommercial 1.0.0 from polyformproject.org and save it as `LICENSE` in the repo root (the license requires using its text verbatim).
2. Add a short header to source files (or at minimum to the README):

   ```
   Copyright (c) 2023–2026 <author name>
   Licensed under the PolyForm Noncommercial License 1.0.0.
   Commercial licensing: see LICENSING.md
   ```

3. Add `CONTRIBUTING.md` with the contributor license grant, and enable a CLA-check bot (e.g. cla-assistant) if outside PRs become frequent.
4. If any past external contributions were merged **before** this policy, get the contributors' written consent to the new license — their code is otherwise still under whatever terms it was submitted.
5. Consider registering the project name as a trademark if commercialization becomes real: the license controls the code, the trademark controls the brand.
