#!/usr/bin/env python3
"""songbook.py — song melodies written by hand, the songwriter's way (references/songwriting.md): the words first, so
the rhythm is the rhythm of speech; a verse, a pre-chorus and a chorus, each doing its job; the title as the hook.
Every song here is original — melody and lyric written for this skill.

    python3 songbook.py              # each song's craft checks (song_metrics)
    python3 songbook.py --out <dir>  # write <dir>/batch.json for song.py and the Melody Lab

A note is N(bar, beat, length, pitch, syllable, vowel): bar 0 is the intro bar (a pickup may sit in it), beats from the
bar's start. The syllable is marked: "*kept" is STRESSED (prosody: it belongs on a strong place), "~" holds the vowel
before it (a melisma). The vowel (a e i o u) drives the sung rendering — the vowel filter's AH EH EE OH OO.
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from melody import Key, chord, surprise  # noqa: E402

VOWEL = {"a": 0, "e": 1, "i": 2, "o": 3, "u": 4}


def N(bar, beat, dur, pitch, syl, v):
    return (bar, beat, dur, pitch, syl, v)


# ── 1. Paper Lanterns — future bass, D major, 75 bpm (a half-time 150) ──────────────────────────────────────────────
PAPER = dict(
    id="song-01", title="Paper Lanterns", style="future-bass", style_label="Future bass (Illenium / San Holo)",
    tonic="D", mode="major", bpm=75, drums="half",
    sections=[("intro", 0, ["vi7"]),
              ("verse", 1, ["vi7", "IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V"]),
              ("pre-chorus", 9, ["IVmaj7", "V", "vi7", "V"]),
              ("chorus", 13, ["IVmaj7", "V", "iii7", "vi7", "IVmaj7", "vi7", "V7", "Iadd9"])],
    lyrics=[("verse", "I kept the light on in the hall"), ("verse", "for every time you said you'd call"),
            ("verse", "the paper lanterns in the rain"), ("verse", "still whisper softly out your name"),
            ("pre-chorus", "I can't let go, I can't let go"), ("pre-chorus", "so I hold on, I hold on"),
            ("chorus", "Paper lanterns, carry me home"), ("chorus", "through the dark I'm not alone"),
            ("chorus", "paper lanterns, light up the way"), ("chorus", "I will find you there someday")],
    craft=["verse low and spoken (D4–B4); lines 1 and 3 one melody, the 3rd ending higher (melodic math)",
           "every verse line a pickup and its last word anticipated, held over the bar line onto colour (maj7, 9th)",
           "pre-chorus: 'I can't let go' twice, a step higher the second time; a 4–3 lean over vi; it hangs on 2 over V",
           "chorus: the title falls 1-7-6-5 ('PA-per LAN-terns'), answered by a run up to 2 ('car-ry me HOME')",
           "line 3 is the hook again, its answer a step higher: the climax F♯5 on 'way', once",
           "after the climax the line walks down the scale, then 7 → 1 on the downbeat over I, held"],
    notes=[
        # verse: line 1 (bars 1–2), line 2 (3–4), line 3 (5–6) = line 1 varied, line 4 (7–8) asks
        N(0, 3.5, .5, "D4", "I", "a"), N(1, 0, .75, "F#4", "*kept", "e"), N(1, .75, .25, "F#4", "the", "e"),
        N(1, 1, 1, "A4", "*light", "a"), N(1, 2, .5, "G4", "on", "o"), N(1, 2.5, .5, "F#4", "in", "i"),
        N(1, 3, .5, "E4", "the", "e"), N(1, 3.5, 2.5, "F#4", "*hall", "o"),
        N(2, 3.5, .5, "E4", "for", "o"), N(3, 0, .75, "F#4", "*ev", "e"), N(3, .75, .25, "F#4", "ry", "i"),
        N(3, 1, 1, "B4", "*time", "a"), N(3, 2, .5, "A4", "you", "u"), N(3, 2.5, .5, "G4", "*said", "e"),
        N(3, 3, .5, "F#4", "you'd", "u"), N(3, 3.5, 2.5, "E4", "*call", "o"),
        N(4, 3.5, .5, "E4", "the", "e"), N(5, 0, .75, "F#4", "*pa", "e"), N(5, .75, .25, "F#4", "per", "e"),
        N(5, 1, 1, "B4", "*lan", "a"), N(5, 2, .5, "A4", "terns", "e"), N(5, 2.5, .5, "F#4", "in", "i"),
        N(5, 3, .5, "E4", "the", "e"), N(5, 3.5, 2.5, "A4", "*rain", "e"),
        N(6, 3.5, .5, "G4", "*still", "i"), N(7, 0, .75, "F#4", "*whis", "i"), N(7, .75, .25, "F#4", "per", "e"),
        N(7, 1, 1, "A4", "*soft", "o"), N(7, 2, .5, "B4", "ly", "i"), N(7, 2.5, .5, "A4", "out", "a"),
        N(7, 3, .5, "G4", "your", "o"), N(7, 3.5, 2, "E4", "*name", "e"),
        # pre-chorus: climbing, repeating, hanging on V — then a breath before the chorus
        N(8, 3.5, .5, "A4", "I", "a"), N(9, 0, .5, "B4", "*can't", "a"), N(9, .5, .5, "C#5", "let", "e"),
        N(9, 1, 1.5, "D5", "*go", "o"), N(9, 2.5, .5, "B4", "I", "a"), N(9, 3, .5, "C#5", "*can't", "a"),
        N(9, 3.5, .5, "D5", "let", "e"), N(10, 0, 2, "E5", "*go", "o"),
        N(10, 3, .5, "C#5", "so", "o"), N(10, 3.5, .5, "D5", "I", "a"), N(11, 0, 1.5, "E5", "*hold", "o"),
        N(11, 1.5, 1, "D5", "on", "o"), N(11, 2.5, .5, "C#5", "I", "a"), N(11, 3, 1, "D5", "*hold", "o"),
        N(12, 0, 2, "E5", "*on", "o"),
        # chorus: the hook and its answer, the hook and its answer reaching the climax, home
        N(13, 0, 1.5, "D5", "*pa", "e"), N(13, 1.5, .5, "C#5", "per", "e"), N(13, 2, 1, "B4", "*lan", "a"),
        N(13, 3, 1, "A4", "terns", "e"),
        N(14, 0, .5, "A4", "*car", "a"), N(14, .5, .5, "B4", "ry", "i"), N(14, 1, .5, "C#5", "me", "i"),
        N(14, 1.5, 2, "E5", "*home", "o"),
        N(15, 0, .75, "C#5", "through", "u"), N(15, .75, .25, "B4", "the", "e"), N(15, 1, 1.5, "A4", "*dark", "a"),
        N(15, 2.5, .5, "F#4", "I'm", "a"), N(15, 3, .5, "A4", "*not", "o"), N(15, 3.5, .5, "B4", "a", "a"),
        N(16, 0, 3, "D5", "*lone", "o"),
        N(17, 0, 1.5, "D5", "*pa", "e"), N(17, 1.5, .5, "C#5", "per", "e"), N(17, 2, 1, "B4", "*lan", "a"),
        N(17, 3, 1, "A4", "terns", "e"),
        N(18, 0, .5, "B4", "*light", "a"), N(18, .5, .5, "C#5", "up", "u"), N(18, 1, .5, "D5", "the", "e"),
        N(18, 1.5, 1.5, "F#5", "*way", "e"), N(18, 3, .5, "E5", "I", "a"), N(18, 3.5, .5, "D5", "will", "i"),
        N(19, 0, 1, "C#5", "*find", "a"), N(19, 1, 1, "B4", "you", "u"), N(19, 2, 1.5, "A4", "*there", "e"),
        N(19, 3.5, .5, "C#5", "some", "u"), N(20, 0, 4, "D5", "*day", "e"),
    ])

# ── 2. Slow Sunday — lo-fi pop, F major, 82 bpm ─────────────────────────────────────────────────────────────────────
SUNDAY = dict(
    id="song-02", title="Slow Sunday", style="lofi", style_label="Lo-fi pop",
    tonic="F", mode="major", bpm=82, drums="lofi",
    sections=[("intro", 0, ["ii7"]),
              ("verse", 1, ["ii7", "V7", "Imaj7", "vi7", "ii7", "V7", "Imaj7", "vi7"]),
              ("pre-chorus", 9, ["IVmaj7", "iii7", "ii7", "V7"]),
              ("chorus", 13, ["IVmaj7", "V7", "iii7", "vi7", "IVmaj7", "iii7", "V7", "Imaj7"])],
    lyrics=[("verse", "coffee's getting cold on the window sill"), ("verse", "you're still dreaming, the morning's still"),
            ("verse", "sunlight spilling over your hair"), ("verse", "I could stay forever right there"),
            ("pre-chorus", "no alarms, nowhere to be"), ("pre-chorus", "just the radio, you and me"),
            ("chorus", "Slow Sunday, stay a little while"), ("chorus", "let the whole world wait outside"),
            ("chorus", "slow Sunday, hold me like a song"), ("chorus", "we were never made to rush along")],
    craft=["7ths and 9ths ON the downbeats (A over Gm7, E over Fmaj7, B♭ over C7) — lo-fi's colour, the 'luxury'",
           "verse lines 1 and 3 one shape (melodic math); line 3 lands on the 7th of C7 — the yearning",
           "pre-chorus: higher and quicker, 'no / nowhere' repeated, hanging on the 3rd of V7",
           "chorus: the title on a lazy rising third (D–F, 'slow SUN-day'), each answer settling on a 7th",
           "line 3: the title again, then the climax G5 on 'song' — the 7th of Am7, once",
           "the end: C7 → Fmaj7, 2 → 1 on the downbeat, held a bar"],
    notes=[
        N(1, 0, .5, "A4", "*cof", "o"), N(1, .5, .5, "G4", "fee's", "i"), N(1, 1, .5, "A4", "*get", "e"),
        N(1, 1.5, .5, "C5", "ting", "i"), N(1, 2, 1, "D5", "*cold", "o"), N(1, 3, .5, "C5", "on", "o"),
        N(1, 3.5, .5, "A4", "the", "e"), N(2, 0, .75, "G4", "*win", "i"), N(2, .75, .25, "A4", "dow", "o"),
        N(2, 1, 2.5, "G4", "*sill", "i"),
        N(2, 3.5, .5, "F4", "you're", "o"), N(3, 0, 1, "A4", "*still", "i"), N(3, 1, 1, "C5", "*dream", "i"),
        N(3, 2, .5, "A4", "ing", "i"), N(3, 2.5, .5, "G4", "the", "e"), N(3, 3, 1, "E5", "*mor", "o"),
        N(4, 0, .5, "D5", "ning's", "i"), N(4, .5, 2.5, "C5", "*still", "i"),
        N(5, 0, .5, "A4", "*sun", "a"), N(5, .5, .5, "G4", "light", "a"), N(5, 1, .5, "A4", "*spil", "i"),
        N(5, 1.5, .5, "C5", "ling", "i"), N(5, 2, 1, "D5", "*o", "o"), N(5, 3, .5, "C5", "ver", "e"),
        N(5, 3.5, .5, "A4", "your", "o"), N(6, 0, 2.5, "Bb4", "*hair", "e"),
        N(7, 0, .5, "A4", "I", "a"), N(7, .5, .5, "G4", "could", "u"), N(7, 1, 1, "A4", "*stay", "e"),
        N(7, 2, .5, "C5", "for", "o"), N(7, 2.5, .5, "D5", "*ev", "e"), N(7, 3, .5, "C5", "er", "e"),
        N(7, 3.5, .5, "A4", "*right", "a"), N(8, 0, 2.5, "A4", "*there", "e"),
        N(8, 3.5, .5, "C5", "no", "o"), N(9, 0, .5, "D5", "a", "a"), N(9, .5, 1, "D5", "*larms", "a"),
        N(9, 1.5, 1, "C5", "*no", "o"), N(9, 2.5, .5, "D5", "where", "e"), N(9, 3, .5, "C5", "to", "u"),
        N(9, 3.5, .5, "D5", "*be", "i"), N(10, 0, 2, "E5", "~", "i"),
        N(10, 3, .5, "D5", "just", "u"), N(10, 3.5, .5, "D5", "the", "e"), N(11, 0, .5, "F5", "*ra", "e"),
        N(11, .5, .5, "E5", "di", "i"), N(11, 1, .5, "D5", "o", "o"), N(11, 1.5, .5, "C5", "*you", "u"),
        N(11, 2, .5, "D5", "and", "a"), N(11, 2.5, 3.5, "E5", "*me", "i"),
        N(13, 0, 1, "D5", "*slow", "o"), N(13, 1, 1.5, "F5", "*sun", "a"), N(13, 2.5, .5, "D5", "day", "e"),
        N(13, 3, .5, "C5", "*stay", "e"), N(13, 3.5, .5, "D5", "a", "a"), N(14, 0, .5, "E5", "*lit", "i"),
        N(14, .5, .5, "D5", "tle", "e"), N(14, 1, 2.5, "C5", "*while", "a"),
        N(15, 0, .5, "A4", "let", "e"), N(15, .5, .5, "C5", "the", "e"), N(15, 1, 1, "E5", "*whole", "o"),
        N(15, 2, .5, "D5", "*world", "e"), N(15, 2.5, 1, "C5", "*wait", "e"), N(15, 3.5, .5, "A4", "out", "a"),
        N(16, 0, 2.5, "C5", "*side", "a"),
        N(17, 0, 1, "D5", "*slow", "o"), N(17, 1, 1.5, "F5", "*sun", "a"), N(17, 2.5, .5, "D5", "day", "e"),
        N(17, 3, .5, "C5", "*hold", "o"), N(17, 3.5, .5, "D5", "me", "i"),
        N(18, 0, .5, "E5", "like", "a"), N(18, .5, .5, "D5", "a", "a"), N(18, 1, 2, "G5", "*song", "o"),
        N(18, 3, .5, "F5", "we", "i"), N(18, 3.5, .5, "E5", "were", "e"),
        N(19, 0, .5, "D5", "*nev", "e"), N(19, .5, .5, "C5", "er", "e"), N(19, 1, 1, "C5", "*made", "e"),
        N(19, 2, .5, "D5", "to", "u"), N(19, 2.5, 1, "C5", "*rush", "u"), N(19, 3.5, .5, "G4", "a", "a"),
        N(20, 0, 4, "F4", "*long", "o"),
    ])

# ── 3. Run With Me — progressive house / NCS anthem, A major, 126 bpm ──────────────────────────────────────────────
RUN = dict(
    id="song-03", title="Run With Me", style="prog-house", style_label="Progressive house anthem (NCS)",
    tonic="A", mode="major", bpm=126, drums="four",
    sections=[("intro", 0, ["vi7"]),
              ("verse", 1, ["vi7", "IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V"]),
              ("pre-chorus", 9, ["IVmaj7", "V", "vi7", "V"]),
              ("chorus", 13, ["IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V", "I"])],
    lyrics=[("verse", "city lights are fading out behind us"), ("verse", "engine humming like a heartbeat in the dark"),
            ("verse", "every road we never took is calling"), ("verse", "and tonight we're gonna leave a mark"),
            ("pre-chorus", "so don't look back, don't look back now"), ("pre-chorus", "the sky is opening"),
            ("chorus", "Run with me, run with me"), ("chorus", "till the morning breaks the night"),
            ("chorus", "run with me, run with me"), ("chorus", "we were made to burn this bright")],
    craft=["verse in running eighths, low (F♯4–E5), dwelling on C♯5 and falling; lines 1 and 3 one shape, the 3rd "
           "landing on C♯5, the maj7 of D", "pre-chorus: 'don't look back' twice, then up to F♯5, held over V",
           "chorus: the title as a one-bar hook (6-5-3), sequenced down a third at once — 'run with me, run with me'",
           "line 3: the hook, then a leap of a minor sixth to the climax A5 — the payoff, once",
           "the end: 7 → 1 over V → I, held"],
    notes=[
        N(1, 0, .5, "C#5", "*ci", "i"), N(1, .5, .5, "C#5", "ty", "i"), N(1, 1, .5, "C#5", "*lights", "a"),
        N(1, 1.5, .5, "B4", "are", "a"), N(1, 2, .5, "C#5", "*fa", "e"), N(1, 2.5, .5, "B4", "ding", "i"),
        N(1, 3, .5, "A4", "*out", "a"), N(1, 3.5, .5, "F#4", "be", "i"), N(2, 0, 1, "A4", "*hind", "a"),
        N(2, 1, 2, "F#4", "us", "u"),
        N(3, 0, .5, "C#5", "*en", "e"), N(3, .5, .5, "C#5", "gine", "i"), N(3, 1, .5, "C#5", "*hum", "u"),
        N(3, 1.5, .5, "B4", "ming", "i"), N(3, 2, .5, "C#5", "like", "a"), N(3, 2.5, .5, "B4", "a", "a"),
        N(3, 3, .5, "E5", "*heart", "a"), N(3, 3.5, .5, "C#5", "beat", "i"), N(4, 0, .5, "B4", "in", "i"),
        N(4, .5, .5, "A4", "the", "e"), N(4, 1, 2.5, "G#4", "*dark", "a"),
        N(5, 0, .5, "C#5", "*ev", "e"), N(5, .5, .5, "C#5", "ery", "i"), N(5, 1, .5, "C#5", "*road", "o"),
        N(5, 1.5, .5, "B4", "we", "i"), N(5, 2, .5, "C#5", "*nev", "e"), N(5, 2.5, .5, "B4", "er", "e"),
        N(5, 3, .5, "A4", "*took", "u"), N(5, 3.5, .5, "F#4", "is", "i"), N(6, 0, 1, "A4", "*cal", "o"),
        N(6, 1, 2, "C#5", "ling", "i"),
        N(7, 0, .5, "A4", "and", "a"), N(7, .5, .5, "B4", "to", "u"), N(7, 1, 1, "C#5", "*night", "a"),
        N(7, 2, .5, "B4", "we're", "i"), N(7, 2.5, .5, "C#5", "gon", "a"), N(7, 3, .5, "D5", "na", "a"),
        N(7, 3.5, 1, "E5", "*leave", "i"), N(8, .5, .5, "D5", "a", "a"), N(8, 1, 2.5, "B4", "*mark", "a"),
        N(8, 3.5, .5, "A4", "so", "o"), N(9, 0, .5, "D5", "*don't", "o"), N(9, .5, .5, "C#5", "*look", "u"),
        N(9, 1, 1, "A4", "*back", "a"), N(9, 2, .5, "D5", "*don't", "o"), N(9, 2.5, .5, "C#5", "look", "u"),
        N(9, 3, 1, "A4", "*back", "a"), N(10, 0, 2, "E5", "*now", "a"),
        N(11, 0, .5, "C#5", "the", "e"), N(11, .5, 1, "E5", "*sky", "a"), N(11, 1.5, .5, "D5", "is", "i"),
        N(11, 2, .5, "E5", "*o", "o"), N(11, 2.5, .5, "F#5", "pen", "e"), N(11, 3, 3, "E5", "ing", "i"),
        N(13, 0, .75, "F#5", "*run", "u"), N(13, .75, .75, "E5", "with", "i"), N(13, 1.5, 2, "C#5", "*me", "i"),
        N(14, 0, .75, "E5", "*run", "u"), N(14, .75, .75, "C#5", "with", "i"), N(14, 1.5, 2, "A4", "*me", "i"),
        N(15, 0, .5, "B4", "till", "i"), N(15, .5, .5, "C#5", "the", "e"), N(15, 1, 1, "E5", "*mor", "o"),
        N(15, 2, .5, "D5", "ning", "i"), N(15, 2.5, 1, "C#5", "*breaks", "e"), N(15, 3.5, .5, "B4", "the", "e"),
        N(16, 0, 3, "C#5", "*night", "a"),
        N(17, 0, .75, "F#5", "*run", "u"), N(17, .75, .75, "E5", "with", "i"), N(17, 1.5, 2, "C#5", "*me", "i"),
        N(18, 0, .75, "A5", "*run", "u"), N(18, .75, .75, "F#5", "with", "i"), N(18, 1.5, 2, "E5", "*me", "i"),
        N(19, 0, .5, "B4", "we", "i"), N(19, .5, .5, "C#5", "were", "e"), N(19, 1, 1, "E5", "*made", "e"),
        N(19, 2, .5, "D5", "to", "u"), N(19, 2.5, 1, "B4", "*burn", "e"), N(19, 3.5, .5, "G#4", "this", "i"),
        N(20, 0, 4, "A4", "*bright", "a"),
    ])

SONGS = [PAPER, SUNDAY, RUN]


def midi(name):
    names = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
    i, acc = 1, 0
    while i < len(name) and name[i] in '#b':
        acc += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + names[name[0]] + acc


def section_of(sections, bar):
    name = sections[0]["name"]
    for s in sections:
        if bar >= s["bar"]:
            name = s["name"]
    return name


def song_metrics(m):
    """The song's craft, measured — what references/songwriting.md asks of a verse, a pre-chorus and a chorus."""
    notes, pcs, roots = m["notes"], m["chord_pcs"], m["chord_roots"]
    ps = [n["p"] for n in notes]
    ivs = [b - a for a, b in zip(ps, ps[1:])]
    bars = len(pcs)
    strong = strong_ct = app = 0
    stressed = stressed_ok = misstress = 0
    for i, n in enumerate(notes):
        b, pos = int(n["t"] // 4), n["t"] % 4
        if pos in (0, 2) or n["d"] >= 1:
            strong += 1
            if n["p"] % 12 in pcs[b]:
                strong_ct += 1
            elif i + 1 < len(notes) and abs(notes[i + 1]["p"] - n["p"]) <= 2 and \
                    notes[i + 1]["p"] % 12 in pcs[int(notes[i + 1]["t"] // 4)]:
                app += 1
        if n["syl"] == "~":
            continue
        on_beat = pos % 1 == 0
        anticipation = not on_beat and n["d"] >= 1                 # the offbeat pushed over the beat: pop's stress
        if n["stress"]:
            stressed += 1
            stressed_ok += on_beat or anticipation
        elif pos in (0, 2) and n["d"] >= 1:
            misstress += 1                                          # a weak syllable held on a strong beat
    top = max(ps)
    tops = [i for i, p in enumerate(ps) if p == top]
    ci = tops[0]
    sec_mean = {}
    for s in m["sections"]:
        sn = [n for n in notes if s["bar"] <= n["t"] // 4 < s["bar"] + s["bars"] and s["name"] != "intro"]
        if sn:
            sec_mean[s["name"]] = round(sum(n["p"] for n in sn) / len(sn), 1)
    misplaced = 0
    for a, b in zip(notes, notes[1:]):
        if b["t"] - (a["t"] + a["d"]) >= 0.49 and not (a["d"] >= 1.5 or b["t"] % 4 >= 2.5):
            misplaced += 1                                          # a rest that is not after a held note nor before a pickup
    last = notes[-1]
    chorus = next(s for s in m["sections"] if s["name"] == "chorus")
    hook = lambda bar: [(round(n["t"] - 4 * bar, 3), n["d"], n["p"]) for n in notes if bar <= n["t"] // 4 < bar + 1]
    name = lambda p: m["key_names"][p % 12] + str(p // 12 - 1)
    return {
        "notes": len(notes), "bars": bars, "range": top - min(ps), "low": name(min(ps)), "high": name(top),
        "steps": round(sum(1 for iv in ivs if 1 <= abs(iv) <= 2) / len(ivs), 3),
        "repeats": round(sum(1 for iv in ivs if iv == 0) / len(ivs), 3),
        "leaps": sum(1 for iv in ivs if abs(iv) >= 5),
        "surprise_bits": round(surprise(ps), 3),
        "strong_chord_tones": round(strong_ct / strong, 3), "appoggiaturas": app,
        "stress_on_beat": round(stressed_ok / max(1, stressed), 3), "misstress": misstress,
        "climax_single": len(tops) == 1, "climax_bar": int(notes[ci]["t"] // 4),
        "climax_in": section_of(m["sections"], int(notes[ci]["t"] // 4)),
        "climax_leap_in": ci > 0 and ps[ci] - ps[ci - 1] >= 3,
        "section_mean_pitch": sec_mean,
        "chorus_above_verse": sec_mean.get("chorus", 0) > sec_mean.get("verse", 0) + 2,
        "hook_returns": hook(chorus["bar"]) == hook(chorus["bar"] + 4),
        "misplaced_rests": misplaced,
        "ends_on_tonic": last["p"] % 12 == m["tonic"], "ends_on_downbeat": last["t"] % 4 == 0,
        "end_length": last["d"], "final_chord_tonic": roots[int(last["t"] // 4)] == m["tonic"],
        "cadence": f"{m['chords'][int(last['t'] // 4) - 1]} → {m['chords'][int(last['t'] // 4)]}",
    }


def lab_melody(s):
    """A song as the Melody Lab and song.py read it: a chord a bar from bar 0 (the intro), notes in beats from bar 0 with
    their syllables, stresses and vowels, the sections and the lyric."""
    key = Key(s["tonic"], s["mode"])
    romans, sections = [], []
    for name, start, rs in s["sections"]:
        sections.append({"name": name, "bar": start, "bars": len(rs)})
        romans += rs
    chords = [chord(r, key) for r in romans]
    notes = []
    for b, a, d, p, syl, v in s["notes"]:
        p = midi(p)
        notes.append({"t": round(4 * b + a, 4), "d": d, "p": p, "name": key.spell(p), "syl": syl.lstrip("*"),
                      "stress": syl.startswith("*"), "vowel": VOWEL[v]})
    m = {"id": s["id"], "title": s["title"], "song": True, "style": s["style"], "style_label": s["style_label"],
         "bpm": s["bpm"], "key": key.name, "tonic": key.tonic, "mode": s["mode"], "key_names": key.names,
         "progression": romans, "chords": [c["name"] for c in chords], "chord_pcs": [c["pcs"] for c in chords],
         "chord_roots": [c["root"] for c in chords], "sections": sections,
         "lyrics": [{"section": sec, "line": ln} for sec, ln in s["lyrics"]], "devices": s["craft"], "notes": notes,
         "form": "song", "form_label": "song — verse, pre-chorus, chorus", "drums": s["drums"], "voice": "sung",
         "audio": [{"label": "sung", "file": f"{s['id']}-sung.mp3"}, {"label": "piano lead", "file": f"{s['id']}-piano.mp3"}]}
    m["metrics"] = song_metrics(m)
    return m


def songs():
    return [lab_melody(s) for s in SONGS]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="a lab folder to write batch.json into")
    a = ap.parse_args()
    ms = songs()
    for m in ms:
        k = m["metrics"]
        print(f"{m['id']} {m['title']} — {m['key']}, {m['bpm']} bpm, {k['bars']} bars, {k['notes']} notes")
        print(f"   range {k['low']}–{k['high']} ({k['range']}) · steps {k['steps']} · repeats {k['repeats']} · "
              f"surprise {k['surprise_bits']} bits · strong chord tones {k['strong_chord_tones']} · leans {k['appoggiaturas']}")
        print(f"   stresses on the beat {k['stress_on_beat']} · misstressed {k['misstress']} · misplaced rests "
              f"{k['misplaced_rests']} · sections {k['section_mean_pitch']} · chorus above verse {k['chorus_above_verse']}")
        print(f"   climax bar {k['climax_bar']} ({k['climax_in']}) single {k['climax_single']} leap in "
              f"{k['climax_leap_in']} · hook returns {k['hook_returns']} · ends {k['cadence']}, tonic "
              f"{k['ends_on_tonic']} on the downbeat {k['ends_on_downbeat']}, {k['end_length']} beats")
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, "batch.json"), "w") as f:
            json.dump({"batch": os.path.basename(os.path.abspath(a.out)), "kind": "songs", "melodies": ms}, f,
                      indent=1, ensure_ascii=False)
        print(f"wrote {os.path.join(a.out, 'batch.json')}")


if __name__ == "__main__":
    main()
