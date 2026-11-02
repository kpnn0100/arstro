# Taste — the approver's verdicts

**Read this first, every time.** The photographer (the user) is the approver; a verdict here outranks
every recipe in references/ and every number in looks.py. When two verdicts agree on something, turn it
into a rule in `scripts/looks.py` or `autograde.py` and say so here.

Format, newest first:

```
## <date> — <set> — <look>  (status: awaiting verdict | judged)
- what was delivered (where), what the grader decided and why
- VERDICT per photo or for the set, in the approver's words
- LESSON: what changes in the rules because of it
```

---

## v3 + THE EXAM (2026-11-02, afternoon) — status: exam awaiting the approver's marks

Eight more verdicts (53): with full depth the approver's GOOD lists got long (the moat: 8 good versions) and the
BAD shrank to Teal & Orange 60 % and Golden Dream / "corrected only" in DAYLIGHT. Changes:
- Art light -> corrected only, even with a lit face (the face took "corrected only"; 3 of the last 4).
- Asakusa settled: Natural Teal (2 of 3). The alley: Golden Dream 50 % again ("impressive"). Never Golden Dream
  or Teal & Orange in daylight.
- Found by the grader's own review of the exam, before the approver saw it: (1) set-matching pulled whites toward
  the set's median highlight tint +0.18 — green, because most frames' brightest pixels are sunlit leaves — and the
  Asakusa facade went green; matching now uses NEAR-NEUTRAL zones only (zone saturation < 0.22). (2) The warm
  highlight wheels sat at 40-45 deg (yellow), which reads GREEN on a white wall; now 30 deg (orange), and the
  daylight white-balance target is tint +0.02 (was +0.04). Asakusa highlight tint +0.23 -> +0.17, whites cream.
  (3) Person photos: matching may raise global saturation by +5 at most (it reaches skin).

THE EXAM — the approver: *"generate me 1 style only that is cinematic, clean, have good depth and have suitable
mood … if i can see over 90% of the photos are good, that mean the skill is ready to edit in real life, write the
summary of your thinking under photo"*. `exam.py build` edits the 16 photos (the two removed excluded) exactly as
a real shoot is edited — `edit.py run`, taste default, nothing tuned for the exam — on a copy of the untouched
originals (`~/photo/grade-lab/exam/exam.cmp`); http://127.0.0.1:8789/exam shows original | edit, the grader's
reading of the photo and every step it took, Good / Not good + why. `exam.py score` reports; >= 90 % = ready.
VERDICT: _(pending)_

## THE TASTE v2 (2026-11-02) — read this first; it is what `edit.py run` does by default

45 verdicts (27 + 18 on 2026-11-02). The approver: *"those bad need to judge carefully, those bad are really
flat, it lose the depth needed for cinematic, learn the definition for cinematic … analyze the characteristic
of each photo and analyze all my comment to know which photo apply which color is the best — basic mechanism
is keep the skin color for human"*.

### What CINEMATIC means to this approver — measured on every judged version (499 renders)
Relative to the other versions of the same photo, the approver's **BEST** had deeper blacks (p1 −0.012),
brighter highlights (p99 +0.018), a wider tonal range (+0.031), more global (sd +0.009) and local contrast
(+0.003) and a little more colour (sat +0.022). The **BAD** were the opposite: lifted blacks (+0.009), DULL
highlights (p99 −0.034), a narrow range (−0.043), less contrast (−0.011 / −0.004), less colour (−0.017).
**Cinematic = the full tonal range (true blacks AND highlights reaching near white) + contrast, global and
local + colour that carries a mood (warm, lonely) while the main subject's colour stays clean + balance.**
Matte, faded, lowered whites = flat = rejected: Moody Forest 70 % was BAD 7 times (sd −0.038, lc −0.014).

Two mechanisms enforce it now: **depth is never scaled** (moods.scale keeps the master curve, clarity,
dehaze, vignette, grain at full — a mood at 50 % is half its colour, full depth; the flat Neon Noir 60 % /
Teal & Orange 60 % were flat only because their curve had been scaled), and **edit.py `depth()`** steepens a
photo's own curve until p1 <= 0.035 and p99 >= 0.90 (night 0.02 / 0.80), within the clipping guard.

### Which photo gets which colour (taste.py v2, from each photo's characteristics and the notes)
| photo characteristics | look | evidence (2026-11-02 notes) |
|---|---|---|
| sky, foliage, buildings, animals, landscape | **Natural Teal** | 14 bests, never bad: Fuji "the mood is very clear, it feel lonely"; Kamakura "really love the sky"; Nara deer "keeping the main object color clean"; sea wall "the balance needed" |
| a person is the subject | **Natural Cinema 70 %** colour, full depth, **skin protected** | the woman (NC 70 %; "just balance, doesn't create a mood" — full depth now adds the mood the 70 % lacked), the teamLab face: "apply a mood with the skin color still correct" |
| still water, reflections | **Neon Noir 60 %** colour at full depth | the pond: "#11 [natural cinema] got the depth but #9 [neon noir 60 %] got the color" |
| night city | **Golden Dream 50 %** | Shinjuku "really have warm mood"; the alley "feel warm" |
| aquarium | **Golden Dream 50 %** | the jellyfish: "i like the water color, teal really hit it" |
| art light (teamLab) | **corrected only**; a lit face: NC 70 % | the orbs: "bring the mood without destroying face color" |
Open question: the Asakusa shopfront took Natural Cinema + contrast ("#9 feel really cinematic, you are so
close") after Natural Teal the round before — street colour may want NC; one more verdict decides.

### The basic mechanism: keep the skin colour for human
`taste.protect_skin`: on any photo with a person (the plan's `subject`, else the skin-hue fraction), the mixer
does nothing to hue on the skin line (8–48°), never desaturates it, lifts it a touch at most — whatever the
look does to the rest of the palette. Plus the earlier rules: no `shadows` lift on a face mask, low luminance NR.

### Removed from the judging (the approver's words)
`_DSF6778.RAF` (Osaka castle through branches: "next time remove this photo, it's too bad to judge") and
`_DSF7014.RAF` (the teamLab fire: "remove this photo too") — lab.py REMOVED; still in the project.

Tokyo re-edited with v2: `~/photo/tokyo/taste-v2/` (tokyo.cmp holds it; v1 kept as `~/photo/tokyo/taste/tokyo.taste-v1.cmp`).

## THE TASTE, as code — `scripts/taste.py` (v1, 2026-10-30 — superseded by v2 above)

27 Grade Lab verdicts on 18 photos (2026-10-30: 9 palette + 1 refine, then 18 taste rounds on photos the
first round had not shown — one invalid, the sea wall's page could not load its images). The taste rounds
CONFIRMED the hypotheses built from the first nine: the two looks made from the verdicts won 12 of 17.

| scene / content | the approver's look (taste.py `choose`) | evidence |
|---|---|---|
| daylight / overcast — sky, foliage, architecture | **Natural Teal** 100 % | won 6 of 7: Kamakura ("really love the sky and the contrast"), Asakusa ("only #1 can achieve the depth, contrast needed"), Nikko ("really have depth"), Osaka castle, Fuji, the pond. The moat took the first cinematic grade (Natural Cinema + contrast "good") |
| … a person, animal or tower is the SUBJECT | **Natural Cinema** + ½ contrast step | the woman (NC + contrast), deer by the temple (NC + contrast, "good depth in color"), pagoda (NC + contrast), deer grazing (NC plain — NC + contrast was BAD there, so half a step) |
| night city | **Natural Cinema 70 %** + warmer | Shinjuku (NC 70 %), the alley (cinematic + warmer: "highlight the important and have good color") |
| aquarium | **Cinematic + warmer** | the jellyfish |
| dark interior, art light | **Corrected only** | teamLab orbs (corrected won); the fire: every look that warmed or opened it was BAD |

**Two mistakes the approver told the skill never to repeat:**
- **Burnt highlights on light sources.** The teamLab fire went LIME under Natural Cinema's first night variant
  (mids opened, +200 K, a warm highlight wheel, a yellow hue move): *"#3 make it so bad on highlight, #9, #10,
  #11 too, make sure this mistake doesn't happen in skill"*. → At night no mid lift, no warming, no hue move in
  the 40–70° band; the clipping guard runs on EVERY render (the lab skipped it); art light gets no look at all.
- **A veil over a face.** *"all bad, why the man's face cover in blur"* — a face mask that lifted `shadows`
  plus luminance NR 22 at ISO 1600. → Face masks lift `exposure` (≤ 0.2) and `texture`/`clarity`, NEVER
  `shadows`; luminance NR is low (10 at ISO 1600, 16 at 3200) — grain reads as film, smear as plastic.

Also: strength preferred ≈ full (mean 0.95); a slight warm bias (warmer won both times it was offered at night
and in water). The Tokyo set re-edited by this profile: `~/photo/tokyo/taste/` (tokyo.cmp now holds it).

## THE TASTE (what the approver's eye has said so far — read this before any grade)

Learned 2026-10-30 from the Grade Lab (9 verdicts on 8 Tokyo photos, data/judgments.jsonl; `lab.py summary`):

1. **Natural object colour first.** The winner (the first cinematic grade, 4 of 8 palette rounds: the woman,
   the deer, Shinjuku, the pagoda) *"keep original color of object well"*. On the woman, 7 of the 9 moods were
   BAD because *"the bad photo fall for one color, it make the photo feel not natural"*. → No global channel
   crossover where there is skin, wood, painted temple red, white walls. Colour work is SELECTIVE (mixer) and
   SUBTLE (wheels), never one colour over everything.
2. **Depth and contrast, never flat.** *"only #8 is good, other make the photo look flat, lost the depth"*
   (pagoda); *"good contrast on human and background"*, *"good overall contrast"* (woman). → Deep toe, rolled
   shoulder, dense sky, subject separated from background (masks, vignette). Lifted/faded/airy looks LOSE
   (Spring Haze and Faded Film were bad on the woman; Spring Haze — picked earlier by name — lost by eye).
3. **Balance, with a story.** *"#4 is balance with a story to tell"* over *"#3 … look like original"*; *"#8 really
   have a mood but #3 got the balance in color and contrast"*. → A mood must not cost balance; "looks like the
   original" is not enough either.
4. **Bold colour where nothing has a true colour.** Landscape and water took the strong moods: Neon Noir on Fuji
   (*"more balance"* than Teal & Orange) and the pond (*"really create a story"*), Golden Dream on the jellyfish
   (*"really impressive, eye catching"*). → Teal foliage, deep cyan sky, complementary colour on a subject
   against water are welcome in nature.
5. **Slightly warm.** The one refine round picked *warmer* (+400 K) over the winner itself.
6. **Night interiors were too dark.** *"all are too dark, only #2 got the balance"* (teamLab). → dark-interior
   median target raised 0.10 → 0.15 (looks.py); night looks open the mids.

Built from it (moods.py, hypotheses until the next verdicts confirm them): **Natural Cinema** (1-3, 5, 6) and
**Natural Teal** (4). A taste round (`lab.py`, kind "taste") puts them beside the winner, the refine pick and
milder versions of the landscape winners, on photos NOT yet judged — starting with the Kamakura temple.

## 2026-10-30 — Tokyo — Spring Haze EDITED PER PHOTO  (status: awaiting verdict)

- VERDICT on the group version (the approver): *"i see you apply color for whole group — that not what i
  want — i need you to see the actual image and apply edit for each image so that it become cinematic, look
  good and have consistent mood — it will later use to replace photo editor: photographer take 1000 photo,
  prompt agent to edit, wala, 1000 photo got edit nicely"*.
- LESSON: **a shared look is not editing.** Every photo is looked at and gets its own edit (subject lifted,
  sky graded, dead areas sunk, the mood's strength per photo); consistency comes from MATCHING each photo's
  shadow and highlight colour and saturation to the set's medians, not from one setting on a group. →
  `edit.py` (review / run), SKILL.md §2 is now this workflow.
- Delivered: `tokyo.cmp` has no groups now — each photo carries correction + Spring Haze + its plan's
  masks (1-2 each) + its match. `~/photo/tokyo/edit/`: plan.json (what the grader saw and decided per photo),
  edit.json (every value + the match log), overview.jpg, sheet_*.jpg, jpg/ (2048 px). The group version is
  `~/photo/tokyo/spring-haze/tokyo.spring-haze-groups.cmp`.
- Two photos missed on the first run and were fixed by their plan entries alone: the Kamakura temple
  (milky, then crushed — the mood at 0.8 with nothing else was right) and the neon alley (a shadows lift on
  the crowd went foggy — exposure alone).
- VERDICT: _(pending)_

## 2026-10-30 — Tokyo / Japan trip — Spring Haze, chosen  (status: judged — "edit each image", see above)

- The approver: *"so try spring haze for me"* — the first mood they picked out of the nine.
- Refined before applying, after looking at it on the whole set: olive foliage (60–80°) now moves to
  fresh mint with the greens (the deer and the mountain stayed olive), sakura pinks lifted harder, and the
  night variant became a real PASTEL NIGHT (mint-navy blacks, pink light) — the first one left the night
  photos looking merely corrected, splitting the set.
- Applied to `tokyo.cmp` (groups "Spring Haze - Day/Night"); the cinematic version is
  `~/photo/tokyo/cinematic-grade/tokyo.cinematic.cmp`. Output `~/photo/tokyo/spring-haze/`: overview.jpg,
  sheet_*.jpg, jpg/ (2048 px), mood.json. Day p50 0.511 · night 0.167.
- VERDICT: _(pending)_

## 2026-10-30 — Tokyo / Japan trip — nine MOODS + a day/night mix  (status: one picked — see above)

- Answer to the verdict below: the correction kept as the floor, a mood on the look groups above it,
  built with the R/G/B curves and the mixer (references/moods.md). Delivered in `~/photo/tokyo/moods/`:
  `board_*.jpg` (every mood on four photos — night street, temple + person, deer, garden pond), and
  per mood `<mood>/overview.jpg` (the whole set), `sheet_*.jpg` (before|after), `tokyo-<mood>.cmp` (a
  copy to open in Cosmo; `tokyo.cmp` itself is unchanged), `mood.json` (the fingerprint). Plus
  `tokyo-spring-neon/` — Spring Haze by day, Neon Noir by night.
- The grader's own read: strongest as a SET — moody-forest, neon-noir, the spring/neon mix,
  mono-noir; boldest — wong-kar-wai and golden-dream (deliberately strong; try strength 0.7).
  Three day variants were fixed after the first board because the overcast temple sky went pink
  (neon-noir), peach (teal-orange) and lilac (spring-haze) — the three channels' shoulders decide a
  white sky's colour.
- VERDICT: _(pending — which moods do you keep, at what strength, for which photos? what would you
  name a mood you want that is not here?)_

## 2026-10-30 — Tokyo / Japan trip (18 RAWs, Fuji X-S10) — `cinematic`  (status: judged)

- Delivered: `~/photo/tokyo/cinematic-grade/` — `jpg/` (2048 px), `sheet_01…05.jpg` (before|after),
  `overview.jpg`, `grade.json` (per photo: scene, witness, every value set, before/after numbers);
  the grade lives in `tokyo.cmp` (groups "Cinematic - Day" and "Cinematic - Night"; the ungraded
  project is `tokyo.before-grade.cmp` there).
- Scenes: night-city 1, 2 · aquarium 5 (jellyfish) · dark-interior 16, 17, 18 (teamLab — set by the
  reviewer; the heuristic said aquarium / night-city) · overcast-day 3, 4, 6, 10, 11, 13, 14 ·
  daylight 7, 8, 9, 12, 15.
- The grader's own judgement of the result: the set reads as one story; strongest 4 (pagoda),
  8 (Fuji: green train-window cast removed, blue sky back), 11, 16–18; weakest 3 (the pale-blue sky
  goes nearly white — the shoulder could hold more of it) and 1–2 (night streets are clean but could
  take more teal).
- VERDICT (2026-10-30, the user): *"I see your edit really well, but this is just basic correction, it look
  good but i need you to do more color blending, use curve and mixer more for apply a mood to the photo,
  try some mood and write that to a skill for each mood — the goal is user can use cosmo and prompt to edit
  a lot of photo"*.
- LESSON: **correction is the floor, not the grade.** The approver accepts the corrections as they are
  (white balance, exposure, highlight protection — keep that pipeline) but a grade must carry a MOOD:
  colour BLENDED through the tones with the R/G/B curves (tinted blacks, crossed channels, warm or
  pink highlights) and the palette REDUCED with the mixer (hues pulled toward two or three, the rest
  muted). The `cinematic` look was too timid to read as a mood. → moods/ (one file per mood),
  `mood.py` (board / apply / strength), and §2b of SKILL.md for editing by prompt.
