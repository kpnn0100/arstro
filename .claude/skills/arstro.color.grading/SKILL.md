---
name: arstro.color.grading
description: Colour grading photos (and video frames) the way a professional colourist does — read the photo's context and light first (metadata, measurement, and LOOKING at it), correct it, then apply a look — so the photographer only checks. EVERY PHOTO IS EDITED ON ITS OWN — look at each one, write its plan (subject, sky, light, local masks), then correct, give it the mood, apply the plan and match it to the set (scripts/edit.py review / run) — so 1000 photos come out edited and consistent from one prompt. Drives Cosmo through its MCP server (`cosmo-cc mcp`: cosmo_analyze, cosmo_preview, cosmo_run, cosmo_state) or the autograde script. Use it whenever photos are to be graded, colour-corrected, white-balanced, "made cinematic", given a film / teal-orange / moody / night / Tokyo neon look, matched as a set, or auto-edited; when the user asks to "grade", "edit", "color" or "make it look like a movie" a Cosmo project (.cmp) or an Interstellar timeline; and on `/arstro.color.grading` — even when grading is one step of a bigger task. Also for MOODS and editing many photos by prompt — "make these moody", "neon cyberpunk", "soft pastel spring", "Wong Kar-wai", "golden", "sad and blue", "black and white", "teal and orange", "faded film", "a bit warmer / less green / stronger" — nine moods built with R/G/B curves and the colour mixer, one file each in moods/, applied to a whole project in seconds (scripts/mood.py). Holds the colour science, the professional workflow, how to read light and scenes, colour blending with curves and mixer (references/moods.md), Cosmo's controls MEASURED (they are not Lightroom's), the autograde pipeline (scripts/autograde.py) and the approver's taste ledger (taste.md).
---

# arstro.color.grading

> **The photographer is the approver.** Their verdicts outrank every recipe here. **Read `taste.md`
> first, every time** — it is what they accepted and rejected, and what that taught.

The user's words that started this (2026-10-30): *"learn all about color grading … the goal is to
make agent understand context, understand the image property, lighting and apply grading for that
photo so that photographer don't have to edit anymore, just need to check … grading the project
tokyo for me cinematic color"*.

**Cosmo does the pixels; this skill does the judgement.** Cosmo measures (`analyze`) and renders;
deciding that a frame is "a night street lit by sodium lamps and neon, and should stay dark" is
grading, and it lives here. Never put a grading decision into Cosmo's code — if the skill needs a
number Cosmo cannot measure, that is a Cosmo task (`arstro.cosmo.core.implement`, R-GRADE).

---

## 0. Before grading anything

1. **Read `taste.md`.**
2. **Know the brief.** "Cinematic" is not one look (references/looks.md §1). If the user named a
   reference (a film, a photographer, a film stock), use it; if not, the default is the
   `cinematic` look in `scripts/looks.py`, and say so.
3. **Have the tools.** Either Cosmo's MCP tools are connected (`cosmo_*` in your tool list — the repo's
   `.mcp.json` registers them), or run the pipeline: `python3 scripts/autograde.py …` (it starts
   `cosmo-cc mcp` itself). Build first if needed: `cmake --build build --target cosmo-cc`.

## 1. The method — the order every colourist works in (references/workflow.md)

1. **Read the photo — context before pixels.** For each photo:
   - `cosmo_analyze node=N before=true` → the metadata (time taken, ISO, shutter, aperture, focal
     length, lens) and the measurement (key, range, percentiles, clipping, the cast overall and per
     zone, hues, skin/sky/foliage/neon).
   - `cosmo_preview node=N before=true` → **look at it.** Name the scene, the light source(s), the
     subject, what must be protected (skin, a white sky, neon, the snow on Fuji).
   - Decide the **scene** (references/light.md §3): daylight, overcast-day, golden-hour, night-city,
     dark-interior, aquarium … The heuristic in `autograde.py classify()` is a first guess;
     **your eyes overrule it** (`--scene 16=dark-interior`). A teamLab room is not an aquarium because
     both are dark and blue.
2. **Correct (primary balance) — make it TRUE before making it pretty.**
   - White balance against the right witness: a white/overcast sky, cloud, snow, a white wall — never
     foliage, never a blue sky, never the neon. Correct toward a slightly WARM, slightly GREEN
     neutral for daylight (warmth ≈ +0.10, tint ≈ +0.04), not dead grey.
   - Exposure: highlights first (nothing important clips), then the median. In daylight the target
     is a FLOOR — a frame of white sky is supposed to be bright.
   - Night is meant to be dark; art light is the artist's; aquarium blue is the water.
3. **Match the set.** Photos of one sequence share one look: put it on a GROUP so it is one decision.
4. **The look (creative grade)** — tone curve, split tone, hue/saturation shaping, texture (grain,
   vignette). Subtle beats loud: a look you notice before the photo is too strong.
5. **Check.** Measure the graded render (`cosmo_analyze node=N`), look at it next to the original
   (`cosmo_preview`), fix what fails: blacks milky? sky clipped? skin off the skin line? a set that
   doesn't hang together on a contact sheet?
6. **Deliver for checking**, not for re-editing: save the project, export JPEGs, make before|after
   sheets, write down per photo what was done and why (`grade.json`), and ask for verdicts → `taste.md`.

## 2. THE WORKFLOW — every photo edited on its own (`scripts/edit.py`)

The approver (2026-10-30): *"i see you apply color for whole group — that not what i want — i need you to
see the actual image and apply edit for each image so that it become cinematic, look good and have
consistent mood … it will later use to replace photo editor: photographer take 1000 photo, prompt agent
to edit, wala, 1000 photo got edit nicely"*. So: **no look groups. Each photo carries its whole edit;
consistency comes from MATCHING, not from sharing one setting.**

```bash
S=.claude/skills/arstro.color.grading/scripts
python3 $S/edit.py review shoot.cmp --out ~/edit/shoot              # 1. as-shot previews + gridded sheets
#   2. LOOK at review_*.jpg (6 photos a sheet, grid at .25/.5/.75) and write <out>/plan.json:
#      per photo — scene, subject, intent, `set` tweaks, masks (where the subject is, where the sky is)
python3 $S/edit.py run shoot.cmp --mood spring-haze --plan ~/edit/shoot/plan.json --out ~/edit/shoot --save
#   3. LOOK at overview.jpg and sheet_*.jpg; for each photo that missed, fix ITS plan entry and re-run it:
python3 $S/edit.py run shoot.cmp --plan ~/edit/shoot/plan.json --out ~/edit/shoot --names a.RAF,b.RAF --save
#   4. export: cosmo_run "export --outdir ~/edit/shoot/jpg --format jpg --quality 90 --long-edge 2048"
```

Per photo, `run` does what a retoucher does in order: **correct** (autograde's closed loop — white
balance against this photo's witness, exposure highlights-first, noise by ISO, the plan's scene) →
**mood** (the mood's day or night curves + mixer written on the photo itself, `strength` per photo) →
**plan** (the agent's `set` tweaks, `k+=v` relative, and its local masks: a subject lifted with a radial,
a sky graded down with a linear, a dead ceiling sunk) → **match** (after every photo has the above, the
set's median shadow cast, highlight cast and saturation per day/night family are the targets; each
photo's OWN R/G/B toe / shoulder and saturation are nudged 70 % of the way — a colourist matching shots
to a hero) → **guard** clipping. A partial re-run (`--names`) keeps the whole set's targets and merges
into `edit.json`, so fixing the few that missed never re-centres the set on them.

**Writing the plan is the agent's real job — LOOK at every photo.** For each: what is the subject and
where (grid coordinates), where is the sky / the dead area / the light, what must not change (skin,
neon, the artist's light), and what the photo needs that the mood cannot know. Typical entries:

| the photo has | plan |
|---|---|
| a subject (person, animal, car, building) | radial on it: `exposure` +0.15…+0.25, `clarity` +6…8; small subject → small rx/ry |
| a big pale or white sky | linear from the horizon (0) up to the top (full): `exposure` −0.2…−0.3, `saturation` +10 |
| haze (through glass, distance) | that linear + `dehaze` +10…15; global `dehaze+=6` |
| an empty dark foreground / ceiling | linear from its edge to the frame edge: `exposure` −0.25…−0.4 |
| a glow worth keeping (neon, lantern, fire) | nothing that lifts the blacks near it; `highlights` guard does the rest |
| a face lit by the scene | a small radial `exposure` +0.15…0.25, `shadows` +10 at most |
| a photo the mood overdoes | `"strength": 0.7…0.8` for that photo only |

**Do not** stack lifts: `shadows+=` on a mood with a lifted toe goes MILKY (Kamakura, first pass);
`contrast+=6` with `blacks-=4` on top of the mood crushed the same roofs BLACK (second pass) — the
answer was the mood at 0.8 and nothing else. A crowd lifted with `shadows +10` in a radial went FOGGY;
`exposure` alone was right. Measure, then look at the photo itself, never only the sheet.

**At 1000 photos**: `review` sheets are 6 photos each → ~170 sheets; read them in order and write terse
plan entries (most photos need only `scene` and one or two masks; a photo with no entry is still
corrected, given the mood and matched). Then `run` once (≈5 s a photo), read the overview in pages of 18,
fix the misses with `--names`. The record (`edit.json`: scene, subject, intent, every value, the match
log) is what the photographer checks against.

## 2a. The automatic pipeline — `scripts/autograde.py` (correction + a group look; superseded by §2)

```bash
S=.claude/skills/arstro.color.grading/scripts
python3 $S/autograde.py survey tokyo.cmp --out ~/photo/tokyo/grade        # read every photo
#   ...LOOK at <out>/before/*.jpg; decide overrides...
python3 $S/autograde.py grade  tokyo.cmp --out ~/photo/tokyo/grade --look cinematic \
        --scene 16=dark-interior,17=dark-interior --save                    # grade, save the project
python3 $S/autograde.py sheet  ~/photo/tokyo/grade                          # before|after sheets again
```

What `grade` does, per the method above: survey (metadata + measurement + classify) → one group per
scene family with the look (`looks.py`), bypassed → per photo, a closed loop on the REAL render
(white balance by a secant walk on temp/tint against the scene's witness and target; exposure toward
the scene's median with highlights taking priority; noise reduction by ISO) → look on → check and trim
(median window, clipping, black point) → previews, `grade.json`, `sheet_*.jpg`. It is idempotent: a
re-run resets each photo and reuses the groups. Photos are keyed by NAME (node ids renumber on
reopen). `--nodes` limits a re-grade to some photos (node ids of the project as it is now).

**After it runs, YOU are the reviewer**: read the sheets, and for each photo that is wrong say why in
the language of §1 — then fix the RULE (looks.py / autograde.py) if it is a rule, or override that
photo (`--scene`, or a `cosmo_run "select N\nset …"` touch-up) if it is the photo. Record what you
learned in references/ or taste.md so the next grade starts smarter.

## 2b. Moods, and editing a lot of photos by prompt

The approver's verdict on the first grade: correction alone is *"just basic correction"* — a grade
carries a MOOD, made by **colour blending with the R/G/B curves and the mixer** (references/moods.md).
**`mood.py` puts a mood on GROUPS — use it to PREVIEW moods (`board`, `apply --save-as` a copy), not to
deliver: the delivered edit is per photo (§2, `edit.py run --mood <the chosen one>`).**

| mood | for | file |
|---|---|---|
| `neon-noir` | night city, neon, rain, cyberpunk | moods/neon-noir.md |
| `teal-orange` | people, street, travel, blockbuster | moods/teal-orange.md |
| `wong-kar-wai` | romantic night, Hong Kong heat, red and green | moods/wong-kar-wai.md |
| `faded-film` | travel diary, nostalgia, matte print | moods/faded-film.md |
| `moody-forest` | dark, earthy, forest, temples, rain | moods/moody-forest.md |
| `spring-haze` | airy pastel, sakura, gardens, tender | moods/spring-haze.md |
| `golden-dream` | warm, summer, golden, happy | moods/golden-dream.md |
| `blue-melancholy` | cold, sad, blue hour, winter | moods/blue-melancholy.md |
| `mono-noir` | black and white, street, Moriyama | moods/mono-noir.md |

**A prompt becomes a command.** Read the prompt for: the MOOD (a name, or its words — `mood.py list`
shows them; `moods.find()` matches them), the STRENGTH ("subtle/a touch" 0.5–0.6, "strong/more"
1.2–1.3), WHICH photos (all · the night ones · the day ones · "the deer" → look at `cosmo_state` names /
the survey), and whether it is a NEW look or an ADJUSTMENT of the current one.

```bash
S=.claude/skills/arstro.color.grading/scripts
python3 $S/mood.py list                                             # the moods and their words
python3 $S/mood.py board  tokyo.cmp --photos a.RAF,b.RAF,c.RAF,d.RAF   # every mood on 4 photos, 1 sheet
python3 $S/mood.py apply  tokyo.cmp --mood "moody dark forest" --strength 0.8 --save-as tokyo-moody.cmp
python3 $S/mood.py apply  tokyo.cmp --mood spring-haze --night-mood neon-noir --save   # day / night mix
```

`apply` on an ungraded project corrects every photo first (autograde), then puts the mood on the
groups; on a graded one it only swaps the groups' look (≈30 s for 18 RAWs). It guards clipping per
photo but lets the mood move the key. It writes before|after sheets, `overview.jpg` (the whole set —
judge a mood as a SET) and `mood.json` (the measured fingerprint — compare with the mood's file).

**Adjustments by prompt** go to the GROUP (one decision for the set) unless the prompt names a photo:

| the user says | do (on the group, `cosmo_run "select <group>\nset …"`) |
|---|---|
| warmer / cooler | `temp` +300…+600 / −300…−600 (group temp adds its offset from 6500) |
| more / less green (cast) | `tint` −8 / +8 (positive = magenta) |
| brighter / darker (mood kept) | master curve mid point ±0.04 (`0.5,0.54`), not exposure |
| more contrast / softer | master curve: toe −0.02 & shoulder +0.02 / toe +0.03 & shoulder −0.03 |
| lifted / faded blacks | master toe +0.03…+0.06 |
| bluer / teal shadows | `curveB` toe +0.02…+0.04, `curveR` 0.25 point −0.02 |
| warmer highlights | `curveB` top −0.03…−0.06 |
| less saturated / more vivid | `saturation` −8 / `vibrance` +8 |
| greens less loud | `mixer1` at 100–150 −0.15 more; `mixer2` there −0.1 |
| skin warmer / healthier | `mixer1` at 25–35 +0.08, `mixer2` +0.05 (never a wheel on the photo: D-68) |
| deeper sky | `mixer2` at 210–230 −0.15 |
| more / less grain | `grainAmount` ±5 |
| stronger / weaker mood | re-apply with `--strength` |
| one photo too dark / bright | that photo's own `exposure` ±0.2 (with `highlights`), not the group |

After any edit: `cosmo_preview` the photos it touched and look, then save (or `--save-as` a copy so the
approver can compare versions). Record what the approver keeps in `taste.md` and in the mood's file.

## 3. Driving Cosmo by hand (MCP)

```
cosmo_api                                  # the grammar and every `set` key, in ENGINE units
cosmo_run   "project open /path/tokyo.cmp"
cosmo_state                                # nodes: address photos by `node` (kind image|group)
cosmo_analyze node=4 before=true           # as shot
cosmo_preview node=4 before=true edge=900  # look
cosmo_run   "select 4\nset temp=5400 tint=30 exposure=0.2 highlights=-40"
cosmo_preview node=4 ; cosmo_analyze node=4   # check
cosmo_run   "project save"
```

Read **references/cosmo-controls.md before setting any value** — Cosmo's `shadows`, `blacks`,
`contrast` and the shadow colour wheel are several times stronger than their Lightroom namesakes,
the tone curve works in a log domain, and a colour wheel's hue ADDS across group levels (D-68).

## 4. Files

| file | what |
|---|---|
| `taste.md` | the approver's verdicts and what they taught — read first |
| `references/color-science.md` | light, colour, the eye, scene vs display, what "cinematic" is made of |
| `references/workflow.md` | the professional order of operations, and the checks |
| `references/light.md` | reading light and scene from metadata + measurement + looking; mixed light; what to neutralise and what not |
| `moods/<mood>.md` | one file per mood: palette, how it is built (curves + mixer), use / avoid, tuning words, measured fingerprint, the approver's verdict |
| `references/moods.md` | colour blending with R/G/B curves and the mixer; how to design a new mood |
| `scripts/edit.py` | THE editor: `review` (gridded sheets to look at), `run` (per-photo correct → mood → plan → match → guard) |
| `scripts/moods.py` · `scripts/mood.py` | the moods as data (+ strength, prompt matching) · list / board / apply |
| `references/looks.md` | a catalogue of looks (cinematic film print, teal-orange, Eterna, Classic Chrome, Tokyo night, golden hour, noir, pastel …) with Cosmo recipes |
| `references/cosmo-controls.md` | every Cosmo control MEASURED on a real photo; pitfalls |
| `scripts/autograde.py` | survey / grade / sheet |
| `scripts/looks.py` | per-scene targets and the looks, in Cosmo `set` keys |
| `scripts/calibrate.py` | measure what a control really does on your own photo |
| `scripts/mcp_client.py` | a minimal MCP stdio client (what autograde uses) |
| `data/tokyo-plan-2026-10-30.json` | a real plan.json: what the grader saw and decided for each of the 18 Tokyo photos — the example to copy |
| `data/tokyo-2026-10-30.json` | the Tokyo grade: scene, reasons and every value per photo |

## 5. Done means

- [ ] Every photo was READ (metadata + measurement + looked at) before it was touched.
- [ ] The correction is justified per photo in `grade.json` (scene, witness, targets hit).
- [ ] The look sits on groups; nothing is clipped that matters; blacks are black, not milky;
      skin is on the skin line; the set hangs together on one contact sheet.
- [ ] The project is saved, reopened and re-measured the same; JPEGs + before|after sheets exist.
- [ ] The photographer has the sheets and the question "what would you change?" — and the answer
      goes into `taste.md`.
