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
