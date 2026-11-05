---
name: arstro.color.grading.style
description: Understand what a photographer MEANS when they ask for a look in their own words, and turn it into the edit. Users write "warm", "make it moody", "tông Hàn nhẹ nhàng", "vintage film but brighter", "black and white", "like a Japanese film", "golden hour", "a bit less orange", "make the night ones neon" — any wording, English or Vietnamese, with or without accents. Holds the STYLE CATALOGUE (17 styles — cinematic, natural, warm, cool, moody, bright & airy, dreamy, melancholy, vintage film, teal & orange, neon, golden hour, vibrant, muted, black & white, Korean tone, Japanese film — about 270 wordings), three candidate readings of each judged by the photographer in the Style Lab, the prompt reader (`style.py match`) and how to apply a style to one photo or a thousand (`edit.py run --style`). Loaded from arstro.color.grading (the entry point) whenever a prompt names a style, feeling, mood, film, era, region or adjustment; use it also on `/arstro.color.grading.style`, when adding a style or a wording, and when the user judges styles at 127.0.0.1:8789/styles.
---

# arstro.color.grading.style

> **Loaded from `arstro.color.grading`**, the entry point — that skill owns the method (read the photo, correct
> it, give it a look, match the set, LOOK again) and the approver's taste. This one owns the **words**: what a user
> means by a style, and which edit answers it. Read the entry skill's `taste.md` first, as always.

The approver (2026-11-05): *"now i need to teach you with multiple style and wording since user will input a lot,
you need to understand what they mean — generate a list of edit that represent for a style, a desire with photo, i
will judge that — each style just give me 3 photo, i will pick the best suitable for the style — then build the
skill to make sure agent can do edit just from prompt"*.

## 1. Read the prompt — five questions, in this order

A prompt is not a keyword. Read it the way a colourist reads a client's brief:

| question | listen for | becomes |
|---|---|---|
| **1. Which look?** | a named look (Korean tone, vintage film, B&W, neon, golden hour, teal & orange, Japanese film) or a feeling (warm, moody, dreamy, sad, bright, vibrant, muted, natural, cinematic) | the BASE style (§3) |
| **2. Refined how?** | a second feeling ("warm AND moody"), adjustment words (warmer, brighter, less contrast, "bớt ấm", "sáng hơn chút") | modifier steps on top of the base |
| **3. How much?** | "a bit / slightly / subtle / hơi / nhẹ / chút" · "very / strong / really / đậm / rất" · "less / not too / bớt / đừng quá" | strength 0.6 · 1.3 · a step the OTHER way |
| **4. Which photos?** | all · "the night ones" · "the day ones" · "the deer" · "photo 3" · "the portraits" | `scope` (night/day) or `--names` from `cosmo_state` |
| **5. New look or a change?** | "make them…" (new) · "a bit warmer", "less orange", "too dark" (change what is there) | a full re-edit, or one step on the current edit |

Rules that decide the hard cases:
- **A named look beats a feeling.** "tông Hàn nhẹ nhàng" = Korean tone, gently — not Dreamy. "warm vintage film" = vintage film, warmer.
- **The longer phrase beats a word inside it.** "golden hour" over "warm"; "Japanese film" over "film"; "film look" (cinematic) over "film" (vintage).
- **"Less X" is never the style X.** "not too warm", "bớt ấm" = the opposite step (cooler), on whatever the photo has.
- **Adjustment words alone are not a style.** "a bit brighter" refines the CURRENT edit (or the taste when there is none) — never re-grade the set for it.
- **A reference counts as a style.** A film (Blade Runner → neon; In the Mood for Love → `wong-kar-wai` mood), a film stock (Portra, Kodak Gold → vintage film), a director (Hamaguchi, Shunji Iwai → Japanese film), a place ("Seoul café" → Korean tone). Not in the catalogue? Pick the nearest style AND say which one you picked and why.
- **Two looks for two groups** ("night ones neon, day ones Korean") = two runs, each with its scope / `--names`.
- **Ambiguous and costly** (1000 photos, two readings equally likely — "make it film" could be vintage or cinematic): render ONE photo both ways (`style.py board --styles a,b` on a copy, or `cosmo_preview` after a `set`) and ask with the pictures — do not guess on a thousand.
- **What the approver's taste forbids still holds inside any style**: skin stays true (`taste.protect_skin`), a white subject stays white, no burnt light sources, skies stay smooth and one colour, a face is never veiled.

`python3 scripts/style.py match "<prompt>"` reads a prompt the way the scripts will — use it to check yourself, and it is
what a small local model gets. **You are better at reading than the matcher**: when it misreads (a sarcastic "great,
even more orange"; a negation it missed), pass the style id you decided (`--style warm`) and the steps yourself.

## 2. Apply it

```bash
G=.claude/skills/arstro.color.grading/scripts; S=.claude/skills/arstro.color.grading.style/scripts
python3 $S/style.py match "tông Hàn nhẹ nhàng, sáng hơn chút"          # how it is read -> style, strength, steps
python3 $G/edit.py run P.cmp --out DIR --plan DIR/plan.json --style "tông Hàn nhẹ nhàng, sáng hơn chút" --save
python3 $G/edit.py run P.cmp --out DIR --style "neon" --names a.RAF,b.RAF   # only some photos
```

`edit.py run --style` is the whole editor (arstro.color.grading §2): every photo READ and corrected on its own, the
style's recipe on it (its night variant on night photos), the plan's masks, the set matched, depth (unless the style
lifts its blacks on purpose), the subject and sky checks, the clipping guard. A night/day scope in the words edits
only those photos and leaves the others as they are. Write the plan first as always (`edit.py review`, LOOK).

**A change to the current edit** ("a bit warmer", "less orange", "too dark") is not a re-run: use the adjustment table
in arstro.color.grading §2b (`cosmo_run "select N\nset temp+=300"` …) on the photos it names, then LOOK.

**Inside Cosmo** (`COSMO_CONTROL` set): the same commands attach to the user's window; answer in a line what you
read ("Korean tone, gently, a step brighter — on all 13 photos") so the user can correct the READING, not only the result.

## 3. The catalogue — `scripts/styles.py`

| style | means | some of the words (all: `style.py list`) |
|---|---|---|
| `cinematic` | a frame from a film: full range, mood in the colour | cinematic, movie, film look, điện ảnh, như phim |
| `natural` | true to life, only fixed | natural, realistic, just fix, tự nhiên, màu thật |
| `warm` | the light leans gold, cozy | warm, cozy, ấm, ấm áp, tông ấm |
| `cool` | blue-cyan air, calm and fresh | cool, fresh, crisp, mát, tông lạnh |
| `moody` | dark, dramatic, dense | moody, dark, dramatic, low key, tối, trầm, huyền bí |
| `bright-airy` | light, open, clean | bright, airy, high key, sáng, trong trẻo |
| `dreamy` | soft, hazy, pastel, romantic | dreamy, soft, ethereal, pastel, mộng mơ, lãng mạn |
| `melancholy` | sad, lonely, still | sad, lonely, gloomy, buồn, cô đơn, u buồn |
| `vintage-film` | an old print, film colour, grain | vintage, retro, film, Kodak, Portra, 90s, hoài cổ, phim cũ |
| `teal-orange` | the blockbuster split | teal and orange, blockbuster, Hollywood, xanh cam |
| `neon` | cyan-magenta night city | neon, cyberpunk, Blade Runner, đèn neon |
| `golden-hour` | the low sun's glow | golden hour, sunset, hoàng hôn, nắng chiều |
| `vibrant` | colour that pops | vibrant, vivid, punchy, pop, HDR, rực rỡ, tươi |
| `muted` | quiet, desaturated, documentary | muted, desaturated, minimal, Classic Chrome, nhạt màu, trầm màu |
| `bw` | tones only | black and white, B&W, monochrome, noir, đen trắng |
| `korean` | K-drama: bright, clean, soft | Korean tone, K-drama, tông Hàn, kiểu Hàn |
| `japanese` | Japanese film: soft, cyan shadows | Japanese film, tông Nhật, Shunji Iwai, Hamaguchi |

Each style has **three candidates** — three honest readings of the word (e.g. warm = a golden glow over everything /
natural colour in warmer light / orange light on the wheels). Which one IS the word is the approver's call, made in
the **Style Lab**; `styles.recipe(style)` returns their pick, or candidate A (the grader's first reading) until they
have judged it — `style.py list` says which. Its `modifier` is what the style adds when it is the SECOND word ("warm
and moody" = moody + `warmer 1.5`).

A candidate: `{look, strength, tweaks, set, night, depth}` — a mood of the entry skill (or `taste` = the approver's look
for that photo, or `corrected`), its colour strength, the shared refine steps (`taste.TWEAKS`), extra Cosmo `set` keys
(`k=v`, or `k+=v` relative to the correction), a night `set`, and whether the cinematic depth floor applies (False for
styles whose lifted blacks ARE the request: faded, airy, dreamy, Korean, Japanese).

**A style may colour its sky** (`style.py sky_tolerance`): the taste keeps every sky as the camera had it, but a user
who asks for warm or golden hour wants the sky warm too. The sky check still refuses what made skies fake — sand,
two colours, clouds torn from their sky, extra contrast. `natural`, `cinematic` and `vibrant` keep the strict sky (a vibrant
request still wants a white sky white — the first board turned it mint).

## 4. The Style Lab — the approver judges what each word means

```bash
python3 scripts/style.py board P.cmp [--styles warm,cool] [--plan plan.json]   # 3 candidates per style, a COPY of P
python3 ../arstro.color.grading/scripts/lab.py serve                            # http://127.0.0.1:8789/styles
python3 scripts/style.py list                                                   # what is judged, what each recipe is now
```

One card per style: the original, the three candidates (shuffled, unnamed), Best / Wrong / "none of them", a note.
Every click is saved at once to `data/style-verdicts.json` (what `recipe()` reads — the next edit already follows it)
and each decision to the entry skill's `data/judgments.jsonl` (`kind: style`). The board renders through the editor's
own steps on the style's demo photo, so the pick is what `edit.py run --style` delivers.

**After the approver judges:**
- write what the picks MEAN into `taste.md` (entry skill) — e.g. "warm = the light warmer, never a gold wash";
- a style judged **none fits** gets three NEW candidates built from the note (or a photo that can carry the word —
  "can not dark and moody on this photo"), and a new board for that style only (`--styles <id>`). **A style rendered
  again is judged again**: `board` moves its verdict into the style's `history`, the page shows it as "Round N — last
  time: …" and opens on the styles still to judge, and `recipe()` falls back to candidate A until the new pick — so
  put the reading the note asks for in A;
- a **Wrong** on a candidate is a lesson about the word ("vintage is not faded to them") — record it beside the style;
- when two styles' picks agree on a rule (e.g. both refuse a coloured sky), make it a rule in `styles.py` / `style.py`;
- a new wording a user used that the matcher missed → add it to that style's `words` (and `style.py match` it).

## 5. Adding a style

1. A user asked for something the catalogue reads wrongly or not at all → name the desire, write `means`, `words`
   (English + Vietnamese, the way people actually type it), `modifier`, a `demo` photo that suits it.
2. Three candidates that are genuinely different READINGS (not one recipe at three strengths). The demo photo must
   be in the project the board renders from (`tokyoaitest.cmp` has 13 of the shoot's photos — a missing one falls
   back to the first photo). **Do not give a candidate a colour-mixer line aimed at one hue** (pinks only, say): in
   Cosmo it moved other hues too (gold ornaments to lime, sakura to cream, 2026-11-05 — suspected engine defect); get
   that colour from the grade wheels, vibrance or a mood instead, and measure the hue you protect after rendering.
3. `style.py board --styles <id>` → LOOK at all three yourself (skies at 1:1, skin, whites) → the approver judges.
4. `style.py match` a few real prompts with the new words, including one with "a bit" and one with "less".

## 6. Files

| file | what |
|---|---|
| `scripts/styles.py` | the catalogue (STYLES), the verdicts (`recipe`, `chosen`), the prompt reader (`match`, `variant`) |
| `scripts/style.py` | `list` · `match "<prompt>"` · `board` (the Style Lab's renders); `resolve`, `edit_photo`, `sky_tolerance` used by edit.py |
| `data/style-verdicts.json` | the approver's pick per style (written by the Style Lab) |
| `../arstro.color.grading/scripts/edit.py` | `run --style "<words>"` — the editor that applies a style to a whole shoot |
| `../arstro.color.grading/scripts/lab.py` | the Grade Lab server; `/styles` is the Style Lab |
