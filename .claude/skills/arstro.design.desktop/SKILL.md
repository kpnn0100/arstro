---
name: arstro.design.desktop
description: SUPERSEDED — the Arstro desktop design system now lives in arstro.design.rule, which covers every front end (desktop, touch and phone shell) rather than desktop alone. This skill remains only so older references resolve; it immediately redirects. Invoke arstro.design.rule instead for any UI work on cosmo, pulsar, genesis, arstrobench, the launcher shell, interstellar or solaris.
---

# arstro.design.desktop — superseded

**Invoke `arstro.design.rule` instead. Stop reading here.**

This skill was the family-wide desktop design system: the token contract, the non-negotiable motion
rule, R1–R6 (smooth · responds immediately · contained · window-responsive · text fits · reachable),
the Artboard-first routing rule, and verify-by-looking.

**All of it moved to `arstro.design.rule`, intact**, on 2026-09-03, and was extended there with what
this file did not carry: the measured type ramp and tracking formulas, the real duration table, the
`layout()` contract, the widget and gesture conventions, the twenty gotchas already paid for, the
every-state checklist, and the consistency-audit procedure.

**Why it moved rather than being edited in place.** Two things were wrong with the split:

- **"Desktop" was the wrong boundary.** The rules that matter — nothing snaps, tokens only, text
  fits, overflow scrolls — bind the touch shell and the phone launcher exactly as hard as they bind
  cosmo's desktop window. R-TOUCH already says the touch shell may diverge in *metrics* and never in
  palette, radii, accent or motion vocabulary; that is one design law with a metric exemption, not
  two design systems.
- **It had become one of three voices.** `arstro.cosmo.design.implement` declared this file "the
  law", `implement_artboard` §2A held the taste rules it was adapted from, and this file restated
  them. Three copies of one rule is how a rule drifts, which is the whole argument for
  `arstro.rule`.

The routing rule this file was most cited for is unchanged and now lives in `arstro.design.rule` §5:
**a genuinely reusable control belongs in Artboard via `implement_artboard`**, with its docs and
tests — never grown as a one-off inside one app where the others cannot share it.

If you were sent here by an older skill's precedence clause, that clause means
`arstro.design.rule`.
