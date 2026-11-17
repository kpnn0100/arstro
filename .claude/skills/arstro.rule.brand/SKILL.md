---
name: arstro.rule.brand
description: The brand law for Arstro and its apps — the Arstro mark (one four point star, cut once into a white ship and a gradient trail), its geometry on the 96 grid, clear space and minimum size, the colours (Star white, Nebula purple to pink, Night), the type (DM Sans, JetBrains Mono, the name always lowercase), lockups, backgrounds, the app icon, what must never be done to the mark, and how each app's logo inherits from it. Invoke BEFORE drawing, placing or changing any logo, wordmark, splash screen, app icon, about box or brand page; alongside arstro.rule.design for UI and arstro.rule.animation for anything that moves. Invoke directly for "add the logo", "make a splash", "make an app icon", "design a logo for <app>", "is this on brand".
---

# arstro.rule.brand

**What Arstro looks like when it signs its name.** The values and every drawing live in one page,
and that page is the copy of record: **`docs/brand/arstro-identity.html`**. Open it before any
brand work; this file is the law, the page is the specimen. `docs/brand/app-logos.html` holds the
app logos, all chosen by the founder on 2026-11-17 (Cosmo's lens, Interstellar's Warp, Solaris's Synth sun), with their geometry and their animations.

`arstro.rule.design` governs the UI, `arstro.rule.animation` governs motion. This file governs the
mark, the name and the brand colours.

## 1. The idea

Arstro is *art* + *astro*: reach the star by precise engineering. **The mark is one four point
star, cut once.** The upper part is a ship (and the letter A); the lower part is its trail. Art is
the star, engineering is the cut. The founder chose it on 2026-11-17 for being clean first and
creative second — that order is the brand's taste and applies to every app logo too.

## 2. The mark

Drawn on a 96 unit grid. Two shapes, both from one star:

| Part | Geometry |
|---|---|
| Star | `M48 5 Q58 38 91 48 Q58 58 48 91 Q38 58 5 48 Q38 38 48 5 Z` |
| Ship | the star, kept above the chevron `(0,74) (48,38) (96,74)` — Star white |
| Trail | the star, kept below the chevron `(0,83) (48,47) (96,83)` — the Nebula gradient |
| Cut | the chevron gap between them: 9 units tall, 37° |

- **Clear space:** one quarter of the mark's height on every side.
- **Minimum size:** mark 16 px; lockup 20 px tall.

## 3. Colour

| Token | Value | Use |
|---|---|---|
| Star white | `#F3F1F8` | the ship; structure |
| Nebula purple | `#7C4DFF` | gradient start (bottom left) |
| Nebula pink | `#FF5FC4` | gradient end (top right) |
| Night | `#141414` | the apps' background |
| Deep space | `#0B0A12` | brand pages |
| Ink | `#17142A` | the ship on a light ground |
| Paper | `#F6F4FB` | light ground |

**White carries the structure; one colour carries the meaning.** The Nebula gradient belongs to
Arstro alone and always runs purple (bottom left) to pink (top right). On the gradient itself the
whole mark is white. The trail is never anything but the gradient or white.

## 4. Type

- The name is **always lowercase**: `arstro`, `cosmo`, `interstellar`, `solaris`.
- **DM Sans SemiBold**, tracking −3%, for names and headings; DM Sans Regular for reading. It is
  the face the apps already embed (`apps/cosmo/assets/fonts/DMSans`).
- **JetBrains Mono** for numbers, versions and technical labels.
- Lockup: the mark is 1.6× the text size, the gap is 0.25× the text size.

## 5. App icon

The mark fills 68% of the icon; corner radius 23% of the side. Three grounds only: Night, Nebula
(all-white mark), Paper (Ink ship).

## 6. Never

Rotate the mark at rest · swap the colours (gradient ship, white trail) · stretch it · recolour the Arstro
trail · add an outline, shadow or glow · close or widen the cut · set the name in capitals.

**The one exception to "never rotate": the ship banks while it flies.** In the landing page's opening
the ship weaves a little and leans up to about 15° into each turn; it is upright the moment it stops.
(Founder, 2026-11-17: "make the logo fly more natural". A nose-first flight across the screen and on
to each planet was tried the same day and withdrawn: "don't make it fly to another planet anymore".)
A still mark, a logo in a layout, an icon: never rotated.

## 7. The apps inherit the principle, not the picture

Founder's word, 2026-11-17: *"don't make it too alike arstro"* and *"cosmo already expresses what
the app is about — do the same"*.

1. **An app logo is two things at once: something from space, and the app's own tool.** Cosmo is
   a camera lens *and* a planet with its moon. That double reading is the rule — it is how a logo
   says what the app does and still belongs to Arstro. A logo that only shows the tool (a play
   button, a piano) is "too normal"; a logo that only shows space says nothing about the app.
   Interstellar: space + video editing (play, cut, clip, timeline). Solaris: a sun + the studio
   (wave, level, knob).
2. **An app logo never reuses the Arstro star or ship.** That shape is Arstro's alone.
3. What is shared is the sentence: **white for the structure, one colour for the meaning, two or
   three flat shapes, clean before clever, readable at 16 px.**

| App | Colour | Logo |
|---|---|---|
| arstro | purple `#7C4DFF` → pink `#FF5FC4` | settled — the star cut into ship and trail |
| cosmo | blue `#4F7EF7` | settled — a white ring and one blue dot |
| interstellar | teal `#14B8A6` → blue `#38A3F5` | settled — **Warp**: a white ship (a play button) with three gradient light streaks (three clips) behind it |
| solaris | orange `#FF6A1A` → yellow `#FFD23F` (a sun) | settled — **Synth sun**: a gradient half sun over three white stripes that shorten like a reflection (level bars). A wave-cut "sunrise on the beach" redraw was tried on 2026-11-17 in three forms and rejected by the founder ("don't work") — do not propose it again. |

Every two-tone runs from the first colour at the bottom left to the second at the top right.
These are **logo** colours. Each app's UI accent is its own token (`arstro.rule.design`); changing
an accent to match its logo is a separate decision for the founder.

A new app logo (pulsar, genesis, …) is proposed as options on that page — each with its anatomy, its small sizes down
to 16 px, and dark/light grounds — and the founder chooses by eye. Do not ship an unchosen option.

## 8. Motion

The mark has one story: a star becomes a ship, and the ship leaves. Show up 1000 ms, disappear
720 ms, working loop 1560 ms — keyframes in the identity page, section 08; the rules they obey are
`arstro.rule.animation`.

## 9. One brand, kept in step — everything that carries it

**A brand change is not done until every row below that it touches has changed, in the same
commit.** The brand lives in four kinds of place, and each has already drifted from another once
(the Solaris logo was redrawn three times in one afternoon and went back; anything updated out of
step would have shipped a logo nobody chose).

**The order is fixed:** the page first (the founder chooses by eye) → the requirement → the code and
its tests → the skills. Never code a logo or a colour the founder has not seen on the page.

| Kind | Where | What it holds |
|---|---|---|
| Pages | `docs/brand/arstro-identity.html` | the Arstro mark: anatomy, colours, type, icon, its three animations |
| | `docs/brand/app-logos.html` | the three app logos: anatomy, storyboards, timing tables |
| | `docs/brand/landing.html` | the public landing page, built as a film (founder, 2026-11-17: "more cinematic", "each app is a planet, it fly through planet horizontally", "text need to layout like a news paper… justify text"). It opens dark with only the Arstro ship, small. Then the stars come on and fall past it, so it reads as climbing without leaving the screen (founder: "make the star around move down so look like the ship is move up continuously"); it flies fast while small for a moment, then the camera closes in (the ship grows evenly while the stars fall faster and grow tails), and it climbs to the top of the front page and stays there as that page's mark. It does not fly on to the planets. The colour comes on and the text appears while the stars are still falling: the ship is still flying for as long as the front page is on screen, and the stars stop only when the camera leaves for a planet (founder: "star only stop moving when move to next planet"). No ring or flash when it stops. Each app is a planet (cosmo: the photo inside the lens ring; interstellar: a planet banded with timeline clips; solaris: a star with the logo's stripes) and the camera flies sideways from one to the next. Each stop is set as a newspaper page by its shape alone: a masthead line, a headline, two justified columns with a drop cap, three briefs. No rules or lines between the text (founder: "remove the line between text… need to keep it wide"). Names stay lowercase even in a masthead. On a wide screen the scroll is locked: it steps through a planet's acts and only then flies on; `?at=<world>.<act>` opens one act, `?nointro` skips the opening, `?static` shows everything at once |
| Code | `apps/cosmo/widgets/BrandMark.{h,cpp}` | the three app logos and their animations, as pure poses + a draw + a Player. Compiled by cosmo, Interstellar and Solaris |
| | `apps/cosmo/widgets/SplashScreen.{h,cpp}` | the opening splash; `Brand` picks the app (logo, word, tagline) |
| | `apps/cosmo/SplashWindow.h` | the splash window for a host with no startup work (Interstellar, Solaris) |
| | `apps/{cosmo,interstellar,solaris}/…/HomeScreen.cpp` | the logo above the wordmark |
| | `apps/interstellar/app/Theme.h`, `apps/solaris/app/Theme.h` | each app's UI accent (cosmo's is the default in `apps/cosmo/Theme.h`) |
| | `apps/*/web/icon.svg`, `apps/*/web/css/tokens.css` | the web face's icon; its tokens are GENERATED from Theme.h (`<app>_web_tokens`) |
| Tests | `cosmo_widget_tests` (`test_brand_…`) | the animation law, measured on poses |
| | `interstellar_app_ui_tests`, `*_web_tokens_current` | the accent on screen; token drift |
| Requirements | cosmo `R-BRAND`, `R-SPLASH-6/7`; Interstellar and Solaris `R-BRAND`, `R-UI-2` | both tiers, in each app |
| Skills | this file, `arstro.rule.animation`, `arstro.interstellar.implement`, `arstro.solaris.implement` | the law, and each app's accent line |

**Logo colours and UI accents are different numbers on purpose.** A logo's two-tone is chosen by
eye; a UI accent must carry white text, so it is the logo's hue taken down to cosmo blue's
luminance (≈ 0.23, white at ≈ 3.8:1):

| App | Logo | UI accent |
|---|---|---|
| cosmo | `#4F7EF7` | `#4F7EF7` |
| interstellar | `#14B8A6` → `#38A3F5` | `#159387` |
| solaris | `#FF6A1A` → `#FFD23F` | `#DD5A0C` |

**Known gaps, not papered over:** the Arstro mark itself is not drawn by any app yet (only the
pages have it); the splash tagline is a C string, not a translated word (Solaris R-ACCESS-2); the
two new splashes have been rendered headlessly but not watched in a live window.

## 10. Updating this rule

The brand is the founder's choice, recorded. Change it only on the founder's word; change the
identity page and this file in the same commit (`arstro.rule` §8).
