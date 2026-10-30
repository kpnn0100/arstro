---
name: arstro.color.grading
description: Colour grading photos (and video frames) the way a professional colourist does — read the photo's context and light first (metadata, measurement, and LOOKING at it), correct it, then apply a look — so the photographer only checks. Drives Cosmo through its MCP server (`cosmo-cc mcp`: cosmo_analyze, cosmo_preview, cosmo_run, cosmo_state) or the autograde script. Use it whenever photos are to be graded, colour-corrected, white-balanced, "made cinematic", given a film / teal-orange / moody / night / Tokyo neon look, matched as a set, or auto-edited; when the user asks to "grade", "edit", "color" or "make it look like a movie" a Cosmo project (.cmp) or an Interstellar timeline; and on `/arstro.color.grading` — even when grading is one step of a bigger task. Holds the colour science, the professional workflow, a catalogue of looks with Cosmo recipes, how to read light and scenes, Cosmo's controls MEASURED (they are not Lightroom's), the autograde pipeline (scripts/autograde.py) and the approver's taste ledger (taste.md).
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

## 2. The pipeline — `scripts/autograde.py`

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
| `references/looks.md` | a catalogue of looks (cinematic film print, teal-orange, Eterna, Classic Chrome, Tokyo night, golden hour, noir, pastel …) with Cosmo recipes |
| `references/cosmo-controls.md` | every Cosmo control MEASURED on a real photo; pitfalls |
| `scripts/autograde.py` | survey / grade / sheet |
| `scripts/looks.py` | per-scene targets and the looks, in Cosmo `set` keys |
| `scripts/calibrate.py` | measure what a control really does on your own photo |
| `scripts/mcp_client.py` | a minimal MCP stdio client (what autograde uses) |
| `data/tokyo-2026-10-30.json` | the Tokyo grade: scene, reasons and every value per photo |

## 5. Done means

- [ ] Every photo was READ (metadata + measurement + looked at) before it was touched.
- [ ] The correction is justified per photo in `grade.json` (scene, witness, targets hit).
- [ ] The look sits on groups; nothing is clipped that matters; blacks are black, not milky;
      skin is on the skin line; the set hangs together on one contact sheet.
- [ ] The project is saved, reopened and re-measured the same; JPEGs + before|after sheets exist.
- [ ] The photographer has the sheets and the question "what would you change?" — and the answer
      goes into `taste.md`.
