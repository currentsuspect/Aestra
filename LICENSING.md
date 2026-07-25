# Aestra — Licensing Guide

Aestra is licensed under the **Aestra Studios Source-Available License (ASSAL) v1.1**.

> **[LICENSE](LICENSE) is the agreement. This page is not.**
>
> This guide exists to make the license easier to understand. It is a plain-language
> orientation, not a restatement of terms, and it has no legal effect. Where anything
> here differs from [LICENSE](LICENSE) — in wording, emphasis, or omission — the text
> of [LICENSE](LICENSE) governs.

---

## What the license is

Source-available, not open source:

- The source is **publicly readable**, for transparency and for learning from.
- The source is **not freely usable**. Reading it grants you no right to ship it, fork it into your own product, or build on it.

Those two facts are the whole model. The rest is detail.

---

## What you can and cannot do

Rather than paraphrase the terms — which is how a summary and a license drift apart — read them directly. They are short:

| Question | Where it is answered |
| --- | --- |
| What am I permitted to do? | [LICENSE §2.1 — Permitted Actions](LICENSE) |
| What am I forbidden from doing? | [LICENSE §2.2 — Restrictions](LICENSE) |
| Who owns what? | [LICENSE §3 — Ownership and Intellectual Property](LICENSE) |
| What happens to code I contribute? | [LICENSE §5 — Contributor License Agreement](LICENSE) |
| Can I use this commercially? | [LICENSE §7 — Commercial Licensing](LICENSE) |

The short version, which is orientation and not terms: **you may read, study, learn from, report bugs against, and propose changes to Aestra. You may not use it in your own software, redistribute it, or build a derivative product from it, without written permission.**

---

## If you are here to contribute

Contributing assigns ownership of your contribution to Aestra Studios. That is a real
consequence and worth understanding before you open a pull request — the terms are in
[LICENSE §5](LICENSE), and they are four short subsections.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the practical workflow.

---

## If you are a student or educator

Reading, studying, analysing and writing about Aestra's architecture are all permitted,
and encouraged. Teaching from the codebase is fine. What is not permitted is taking the
code into your own projects, or redistributing it to a class — the boundary is
[LICENSE §2.2](LICENSE), and it is the same boundary that applies to everyone.

---

## SPDX identifier

```
SPDX-License-Identifier: ASSAL
```

Source files carry a one-line header:

```cpp
// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
```

---

## Why source-available rather than open source

This is the part of this page that is genuinely additional rather than a pointer, so it
is the part worth reading here.

Aestra is a solo-built DAW competing with products backed by companies. An open-source
license would allow a larger company to take the work, ship it, and outspend it on
distribution — while the original project keeps the maintenance burden and none of the
return. Source-available keeps the code readable, which is most of what open source is
actually valued for by the people reading it, without giving away the one asset a small
project has.

It is a deliberate trade: transparency without the right to take. If that makes Aestra
unsuitable for your purposes, that is a fair conclusion to reach, and the license is
stated plainly so you can reach it early rather than late.

---

## Commercial licensing

The Aestra application itself is free for personal use, and intended to stay that way
([LICENSE §7.1](LICENSE)).

Separately from the repository license, the product has paid tiers — currently
**Core**, **Supporter** and **Founder** — which unlock premium modules. Those tiers
govern the *application*, not this source repository, and are documented in
[docs/about/license-reference.md](docs/about/license-reference.md).

For any use beyond personal — embedding Aestra in a product, offering it as a paid
service, or using it commercially — see [LICENSE §7.2](LICENSE) and get in touch.

---

## Contact

| Purpose | Where |
| --- | --- |
| Licensing, commercial and partnership inquiries | [makoridylan@gmail.com](mailto:makoridylan@gmail.com) |
| Bugs and technical questions | [GitHub Issues](https://github.com/currentsuspect/Aestra/issues) |
| Everything else | [GitHub Discussions](https://github.com/currentsuspect/Aestra/discussions) |

---

## Related

- **[LICENSE](LICENSE)** — the agreement itself, and the only authoritative text
- **[NOTICE](NOTICE)** — attribution and third-party notices
- **[docs/about/license-reference.md](docs/about/license-reference.md)** — the product's paid tiers and what they unlock
- **[CONTRIBUTING.md](CONTRIBUTING.md)** — how to contribute
- **[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)** — community guidelines

---

*Copyright © 2026 Dylan Makori / Aestra Studios. Licensed under ASSAL v1.1.*
