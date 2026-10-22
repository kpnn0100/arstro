#!/usr/bin/env python3
"""songbook.py — songs written by hand, the songwriter's way (references/songwriting.md): the words first, so the
rhythm is the rhythm of speech; every section doing its job; the title as the hook. Every song here is original —
melody and lyric written for this skill.

    python3 songbook.py              # each song's craft checks (song_metrics)
    python3 songbook.py --out <dir>  # write <dir>/batch.json for song.py and the Melody Lab

A song is PARTS (verse, pre, chorus, bridge, tag — each its chords, a bar a chord, its notes and its lyric lines) and a
FORM that strings them into a whole song: an intro teasing the hook, verse 1, the pre-chorus, the chorus, a drop on the
hook, verse 2 (the verse's melody, new words), the chorus again, a bridge that turns the story, the last chorus, a tag.

A note is N(bar, beat, length, pitch, syllable, vowel), the bar counted from its part's first (a pickup sits in bar −1,
the bar before). The syllable is marked: "*kept" is STRESSED (prosody: it belongs on a strong place), "~" holds the
vowel before it (a melisma). The vowel (a e i o u) drives the sung rendering — the vowel filter's AH EH EE OH OO.
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from melody import Key, chord, surprise  # noqa: E402

VOWEL = {"a": 0, "e": 1, "i": 2, "o": 3, "u": 4}


def N(bar, beat, dur, pitch, syl, v):
    return (bar, beat, dur, pitch, syl, v)


def P(chords, notes, lines):
    return {"chords": chords, "notes": notes, "lines": lines}


def S(label, kind, part=None, **opts):
    """A section of the form: its label, its kind (what the arrangement does there), the part it sings, and options —
    hook=(part, from bar, bars): the instrumental hook plays that part's melody; chords=[…] (an instrumental section's);
    words=[(syllable, vowel) …] new words on the part's notes (verse 2); lines=[…] their lyric; harmony=True: a harmony
    voice; last_chord: the section's last chord changed (a deceptive cadence before the tag)."""
    return (label, kind, part, opts)


def W(text):
    """Words as the songbook writes them: 'your:o *coat:o is:i' → [(syllable, vowel) …]."""
    return [tuple(w.split(":")) for w in text.split()]


# ── 1. Paper Lanterns — future bass, D major, 75 bpm (a half-time 150) ──────────────────────────────────────────────
PAPER_CHORUS = ["IVmaj7", "V", "iii7", "vi7", "IVmaj7", "vi7", "V7", "Iadd9"]
PAPER = dict(
    id="song-01", title="Paper Lanterns", style="future-bass", style_label="Future bass (Illenium / San Holo)",
    tonic="D", mode="major", bpm=75, drums="half",
    story="Someone keeps a light on for a person who is not coming home; in the bridge they let the lanterns rise — and "
          "let the person go.",
    parts=dict(
        verse=P(["vi7", "IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V"], [
            N(-1, 3.5, .5, "D4", "I", "a"), N(0, 0, .75, "F#4", "*kept", "e"), N(0, .75, .25, "F#4", "the", "e"),
            N(0, 1, 1, "A4", "*light", "a"), N(0, 2, .5, "G4", "on", "o"), N(0, 2.5, .5, "F#4", "in", "i"),
            N(0, 3, .5, "E4", "the", "e"), N(0, 3.5, 2.5, "F#4", "*hall", "o"),
            N(1, 3.5, .5, "E4", "for", "o"), N(2, 0, .75, "F#4", "*ev", "e"), N(2, .75, .25, "F#4", "ry", "i"),
            N(2, 1, 1, "B4", "*time", "a"), N(2, 2, .5, "A4", "you", "u"), N(2, 2.5, .5, "G4", "*said", "e"),
            N(2, 3, .5, "F#4", "you'd", "u"), N(2, 3.5, 2.5, "E4", "*call", "o"),
            N(3, 3.5, .5, "E4", "the", "e"), N(4, 0, .75, "F#4", "*pa", "e"), N(4, .75, .25, "F#4", "per", "e"),
            N(4, 1, 1, "B4", "*lan", "a"), N(4, 2, .5, "A4", "terns", "e"), N(4, 2.5, .5, "F#4", "in", "i"),
            N(4, 3, .5, "E4", "the", "e"), N(4, 3.5, 2.5, "A4", "*rain", "e"),
            N(5, 3.5, .5, "G4", "*still", "i"), N(6, 0, .75, "F#4", "*whis", "i"), N(6, .75, .25, "F#4", "per", "e"),
            N(6, 1, 1, "A4", "*soft", "o"), N(6, 2, .5, "B4", "ly", "i"), N(6, 2.5, .5, "A4", "out", "a"),
            N(6, 3, .5, "G4", "your", "o"), N(6, 3.5, 2, "E4", "*name", "e"),
        ], ["I kept the light on in the hall", "for every time you said you'd call",
            "the paper lanterns in the rain", "still whisper softly out your name"]),
        pre=P(["IVmaj7", "V", "vi7", "V"], [
            N(-1, 3.5, .5, "A4", "I", "a"), N(0, 0, .5, "B4", "*can't", "a"), N(0, .5, .5, "C#5", "let", "e"),
            N(0, 1, 1.5, "D5", "*go", "o"), N(0, 2.5, .5, "B4", "I", "a"), N(0, 3, .5, "C#5", "*can't", "a"),
            N(0, 3.5, .5, "D5", "let", "e"), N(1, 0, 2, "E5", "*go", "o"),
            N(1, 3, .5, "C#5", "so", "o"), N(1, 3.5, .5, "D5", "I", "a"), N(2, 0, 1.5, "E5", "*hold", "o"),
            N(2, 1.5, 1, "D5", "on", "o"), N(2, 2.5, .5, "C#5", "I", "a"), N(2, 3, 1, "D5", "*hold", "o"),
            N(3, 0, 2, "E5", "*on", "o"),
        ], ["I can't let go, I can't let go", "so I hold on, I hold on"]),
        chorus=P(PAPER_CHORUS, [
            N(0, 0, 1.5, "D5", "*pa", "e"), N(0, 1.5, .5, "C#5", "per", "e"), N(0, 2, 1, "B4", "*lan", "a"),
            N(0, 3, 1, "A4", "terns", "e"),
            N(1, 0, .5, "A4", "*car", "a"), N(1, .5, .5, "B4", "ry", "i"), N(1, 1, .5, "C#5", "me", "i"),
            N(1, 1.5, 2, "E5", "*home", "o"),
            N(2, 0, .75, "C#5", "through", "u"), N(2, .75, .25, "B4", "the", "e"), N(2, 1, 1.5, "A4", "*dark", "a"),
            N(2, 2.5, .5, "F#4", "I'm", "a"), N(2, 3, .5, "A4", "*not", "o"), N(2, 3.5, .5, "B4", "a", "a"),
            N(3, 0, 3, "D5", "*lone", "o"),
            N(4, 0, 1.5, "D5", "*pa", "e"), N(4, 1.5, .5, "C#5", "per", "e"), N(4, 2, 1, "B4", "*lan", "a"),
            N(4, 3, 1, "A4", "terns", "e"),
            N(5, 0, .5, "B4", "*light", "a"), N(5, .5, .5, "C#5", "up", "u"), N(5, 1, .5, "D5", "the", "e"),
            N(5, 1.5, 1.5, "F#5", "*way", "e"), N(5, 3, .5, "E5", "I", "a"), N(5, 3.5, .5, "D5", "will", "i"),
            N(6, 0, 1, "C#5", "*find", "a"), N(6, 1, 1, "B4", "you", "u"), N(6, 2, 1.5, "A4", "*there", "e"),
            N(6, 3.5, .5, "C#5", "some", "u"), N(7, 0, 4, "D5", "*day", "e"),
        ], ["Paper lanterns, carry me home", "through the dark I'm not alone",
            "paper lanterns, light up the way", "I will find you there someday"]),
        # the bridge: lower, slower words, then climbing as the lanterns rise — hanging on 2 over V, a breath, the last
        # chorus answers it
        bridge=P(["IVmaj7", "V", "iii7", "vi7", "IVmaj7", "V", "IVmaj7", "V"], [
            N(-1, 3.5, .5, "F#4", "so", "o"), N(0, 0, 1, "B4", "*when", "e"), N(0, 1, .5, "A4", "the", "e"),
            N(0, 1.5, 1, "B4", "*morn", "o"), N(0, 2.5, .5, "A4", "ing", "i"), N(0, 3, 1, "G4", "*finds", "a"),
            N(1, 0, .5, "F#4", "me", "i"), N(1, .5, 2.5, "E4", "*here", "i"),
            N(1, 3.5, .5, "A4", "with", "i"), N(2, 0, 1, "C#5", "*all", "o"), N(2, 1, .5, "B4", "these", "i"),
            N(2, 1.5, 1, "C#5", "*lan", "a"), N(2, 2.5, .5, "B4", "terns", "e"), N(2, 3, 1, "A4", "*burn", "e"),
            N(3, 0, .5, "G4", "ing", "i"), N(3, .5, 2.5, "F#4", "*clear", "i"),
            N(3, 3.5, .5, "A4", "I'll", "a"), N(4, 0, 1, "B4", "*let", "e"), N(4, 1, .5, "C#5", "them", "e"),
            N(4, 1.5, 1, "D5", "*rise", "a"), N(4, 2.5, .5, "D5", "and", "a"), N(4, 3, 1, "E5", "*fill", "i"),
            N(5, 0, .5, "D5", "the", "e"), N(5, .5, 2.5, "E5", "*sky", "a"),
            N(5, 3.5, .5, "C#5", "and", "a"), N(6, 0, 1, "D5", "*let", "e"), N(6, 1, .5, "C#5", "you", "u"),
            N(6, 1.5, 4.5, "E5", "*go", "o"),
        ], ["so when the morning finds me here", "with all these lanterns burning clear",
            "I'll let them rise and fill the sky", "and let you go"]),
        tag=P(["IVmaj7", "V7", "Iadd9", "Iadd9"], [
            N(0, 0, 1.5, "D5", "*pa", "e"), N(0, 1.5, .5, "C#5", "per", "e"), N(0, 2, 1, "B4", "*lan", "a"),
            N(0, 3, .5, "A4", "terns", "e"), N(0, 3.5, .5, "D5", "I'll", "a"), N(1, 0, 1, "C#5", "*find", "a"),
            N(1, 1, 1, "B4", "you", "u"), N(1, 2, 1.5, "A4", "*there", "e"), N(1, 3.5, .5, "C#5", "some", "u"),
            N(2, 0, 8, "D5", "*day", "e"),
        ], ["paper lanterns, I'll find you there someday"]),
    ),
    form=[S("intro", "intro", hook=("chorus", 0, 4), chords=PAPER_CHORUS[:4]),
          S("verse", "verse", "verse"), S("pre-chorus", "pre", "pre"), S("chorus", "chorus", "chorus"),
          S("drop", "drop", hook=("chorus", 0, 8), chords=PAPER_CHORUS),
          S("verse 2", "verse", "verse", words=W(
              "your:o *coat:o is:i *hang:a ing:i by:a the:e *door:o "
              "I:a *don't:o re:i *mem:e ber:e what:a it's:i *for:o "
              "the:e *win:i ter:e *came:e and:a went:e a:a *gain:e "
              "I:a *light:a them:e *up:u and:a say:e your:o *name:e"),
            lines=["your coat is hanging by the door", "I don't remember what it's for",
                   "the winter came and went again", "I light them up and say your name"]),
          S("pre-chorus 2", "pre", "pre"), S("chorus 2", "chorus", "chorus", harmony=True),
          S("bridge", "bridge", "bridge"),
          S("last chorus", "chorus", "chorus", harmony=True, last_chord="vi7"),
          S("tag", "tag", "tag", harmony=True)],
    craft=["the form: an intro teasing the hook, verse, pre-chorus, chorus, a drop on the hook, verse 2, chorus, a bridge, "
           "the last chorus, a tag", "verse low and spoken (D4–B4); lines 1 and 3 one melody, the 3rd ending higher",
           "pre-chorus: 'I can't let go' twice, a step higher the second time; it hangs on 2 over V",
           "chorus: the title falls 1-7-6-5, answered by a run up to 2 — then the hook again, its answer a step higher to "
           "the climax F♯5 on 'way'", "the bridge turns the story: low, then climbing as the lanterns rise — 'and let you "
           "go' hangs on 2", "the last chorus ends on vi (a deceptive cadence), so the tag sings the line again and lands "
           "home: 7 → 1 over V7 → I, held"],
)

# ── 2. Slow Sunday — lo-fi pop, F major, 82 bpm ─────────────────────────────────────────────────────────────────────
SUNDAY_CHORUS = ["IVmaj7", "V7", "iii7", "vi7", "IVmaj7", "iii7", "V7", "Imaj7"]
SUNDAY = dict(
    id="song-02", title="Slow Sunday", style="lofi", style_label="Lo-fi pop",
    tonic="F", mode="major", bpm=82, drums="lofi",
    story="A morning with nowhere to be; by the bridge Monday is coming, and they ask for the day to last.",
    parts=dict(
        verse=P(["ii7", "V7", "Imaj7", "vi7", "ii7", "V7", "Imaj7", "vi7"], [
            N(0, 0, .5, "A4", "*cof", "o"), N(0, .5, .5, "G4", "fee's", "i"), N(0, 1, .5, "A4", "*get", "e"),
            N(0, 1.5, .5, "C5", "ting", "i"), N(0, 2, 1, "D5", "*cold", "o"), N(0, 3, .5, "C5", "on", "o"),
            N(0, 3.5, .5, "A4", "the", "e"), N(1, 0, .75, "G4", "*win", "i"), N(1, .75, .25, "A4", "dow", "o"),
            N(1, 1, 2.5, "G4", "*sill", "i"),
            N(1, 3.5, .5, "F4", "you're", "o"), N(2, 0, 1, "A4", "*still", "i"), N(2, 1, 1, "C5", "*dream", "i"),
            N(2, 2, .5, "A4", "ing", "i"), N(2, 2.5, .5, "G4", "the", "e"), N(2, 3, 1, "E5", "*mor", "o"),
            N(3, 0, .5, "D5", "ning's", "i"), N(3, .5, 2.5, "C5", "*still", "i"),
            N(4, 0, .5, "A4", "*sun", "a"), N(4, .5, .5, "G4", "light", "a"), N(4, 1, .5, "A4", "*spil", "i"),
            N(4, 1.5, .5, "C5", "ling", "i"), N(4, 2, 1, "D5", "*o", "o"), N(4, 3, .5, "C5", "ver", "e"),
            N(4, 3.5, .5, "A4", "your", "o"), N(5, 0, 2.5, "Bb4", "*hair", "e"),
            N(6, 0, .5, "A4", "I", "a"), N(6, .5, .5, "G4", "could", "u"), N(6, 1, 1, "A4", "*stay", "e"),
            N(6, 2, .5, "C5", "for", "o"), N(6, 2.5, .5, "D5", "*ev", "e"), N(6, 3, .5, "C5", "er", "e"),
            N(6, 3.5, .5, "A4", "*right", "a"), N(7, 0, 2.5, "A4", "*there", "e"),
        ], ["coffee's getting cold on the window sill", "you're still dreaming, the morning's still",
            "sunlight spilling over your hair", "I could stay forever right there"]),
        pre=P(["IVmaj7", "iii7", "ii7", "V7"], [
            N(-1, 3.5, .5, "C5", "no", "o"), N(0, 0, .5, "D5", "a", "a"), N(0, .5, 1, "D5", "*larms", "a"),
            N(0, 1.5, 1, "C5", "*no", "o"), N(0, 2.5, .5, "D5", "where", "e"), N(0, 3, .5, "C5", "to", "u"),
            N(0, 3.5, .5, "D5", "*be", "i"), N(1, 0, 2, "E5", "~", "i"),
            N(1, 3, .5, "D5", "just", "u"), N(1, 3.5, .5, "D5", "the", "e"), N(2, 0, .5, "F5", "*ra", "e"),
            N(2, .5, .5, "E5", "di", "i"), N(2, 1, .5, "D5", "o", "o"), N(2, 1.5, .5, "C5", "*you", "u"),
            N(2, 2, .5, "D5", "and", "a"), N(2, 2.5, 3.5, "E5", "*me", "i"),
        ], ["no alarms, nowhere to be", "just the radio, you and me"]),
        chorus=P(SUNDAY_CHORUS, [
            N(0, 0, 1, "D5", "*slow", "o"), N(0, 1, 1.5, "F5", "*sun", "a"), N(0, 2.5, .5, "D5", "day", "e"),
            N(0, 3, .5, "C5", "*stay", "e"), N(0, 3.5, .5, "D5", "a", "a"), N(1, 0, .5, "E5", "*lit", "i"),
            N(1, .5, .5, "D5", "tle", "e"), N(1, 1, 2.5, "C5", "*while", "a"),
            N(2, 0, .5, "A4", "let", "e"), N(2, .5, .5, "C5", "the", "e"), N(2, 1, 1, "E5", "*whole", "o"),
            N(2, 2, .5, "D5", "*world", "e"), N(2, 2.5, 1, "C5", "*wait", "e"), N(2, 3.5, .5, "A4", "out", "a"),
            N(3, 0, 2.5, "C5", "*side", "a"),
            N(4, 0, 1, "D5", "*slow", "o"), N(4, 1, 1.5, "F5", "*sun", "a"), N(4, 2.5, .5, "D5", "day", "e"),
            N(4, 3, .5, "C5", "*hold", "o"), N(4, 3.5, .5, "D5", "me", "i"),
            N(5, 0, .5, "E5", "like", "a"), N(5, .5, .5, "D5", "a", "a"), N(5, 1, 2, "G5", "*song", "o"),
            N(5, 3, .5, "F5", "we", "i"), N(5, 3.5, .5, "E5", "were", "e"),
            N(6, 0, .5, "D5", "*nev", "e"), N(6, .5, .5, "C5", "er", "e"), N(6, 1, 1, "C5", "*made", "e"),
            N(6, 2, .5, "D5", "to", "u"), N(6, 2.5, 1, "C5", "*rush", "u"), N(6, 3.5, .5, "G4", "a", "a"),
            N(7, 0, 4, "F4", "*long", "o"),
        ], ["Slow Sunday, stay a little while", "let the whole world wait outside",
            "slow Sunday, hold me like a song", "we were never made to rush along"]),
        bridge=P(["IVmaj7", "iii7", "ii7", "Imaj7", "IVmaj7", "iii7", "ii7", "V7"], [
            N(-1, 3.5, .5, "C5", "and", "a"), N(0, 0, 1, "D5", "*when", "e"), N(0, 1, .5, "C5", "the", "e"),
            N(0, 1.5, 1, "A4", "*mon", "a"), N(0, 2.5, .5, "G4", "day", "e"), N(0, 3, 1, "F4", "*comes", "u"),
            N(1, 0, .5, "G4", "a", "a"), N(1, .5, 2.5, "A4", "*round", "a"),
            N(1, 3.5, .5, "G4", "we'll", "i"), N(2, 0, 1, "Bb4", "*lose", "u"), N(2, 1, .5, "A4", "our", "a"),
            N(2, 1.5, 1, "Bb4", "*selves", "e"), N(2, 2.5, .5, "C5", "in", "i"), N(2, 3, 1, "D5", "*all", "o"),
            N(3, 0, .5, "C5", "the", "e"), N(3, .5, 2.5, "A4", "*sound", "a"),
            N(3, 3.5, .5, "C5", "but", "u"), N(4, 0, 1, "D5", "*for", "o"), N(4, 1, .5, "E5", "to", "u"),
            N(4, 1.5, 1, "F5", "*day", "e"), N(4, 2.5, .5, "E5", "the", "e"), N(4, 3, 1, "D5", "*world", "e"),
            N(5, 0, .5, "C5", "can", "a"), N(5, .5, 2.5, "E5", "*wait", "e"),
            N(5, 3.5, .5, "D5", "so", "o"), N(6, 0, 1, "F5", "*stay", "e"), N(6, 1, .5, "E5", "with", "i"),
            N(6, 1.5, 1, "D5", "*me", "i"), N(6, 2.5, .5, "C5", "it's", "i"), N(6, 3, 1, "D5", "*get", "e"),
            N(7, 0, .5, "D5", "ting", "i"), N(7, .5, 2, "E5", "*late", "e"),
        ], ["and when the Monday comes around", "we'll lose ourselves in all the sound",
            "but for today the world can wait", "so stay with me, it's getting late"]),
        tag=P(["IVmaj7", "V7", "Imaj7", "Imaj7"], [
            N(0, 0, 1, "D5", "*slow", "o"), N(0, 1, 1.5, "F5", "*sun", "a"), N(0, 2.5, .5, "D5", "day", "e"),
            N(0, 3, .5, "F5", "we", "i"), N(0, 3.5, .5, "E5", "were", "e"), N(1, 0, .5, "D5", "*nev", "e"),
            N(1, .5, .5, "C5", "er", "e"), N(1, 1, 1, "C5", "*made", "e"), N(1, 2, .5, "D5", "to", "u"),
            N(1, 2.5, 1, "C5", "*rush", "u"), N(1, 3.5, .5, "G4", "a", "a"), N(2, 0, 8, "F4", "*long", "o"),
        ], ["slow Sunday, we were never made to rush along"]),
    ),
    form=[S("intro", "intro", hook=("chorus", 0, 4), chords=SUNDAY_CHORUS[:4]),
          S("verse", "verse", "verse"), S("pre-chorus", "pre", "pre"), S("chorus", "chorus", "chorus"),
          S("verse 2", "verse", "verse", words=W(
              "*rain:e is:i *fall:o ing:i *soft:o on:o the:e *win:i dow:o *pane:e "
              "the:e *old:o *song:o is:i *play:e ing:i a:a *gain:e "
              "*la:e zy:i *cat:a a:a *sleep:i on:o the:e *chair:e "
              "we:i don't:o *need:i to:u *go:o an:e y:i *where:e"),
            lines=["rain is falling soft on the window pane", "the old song is playing again",
                   "lazy cat asleep on the chair", "we don't need to go anywhere"]),
          S("pre-chorus 2", "pre", "pre"), S("chorus 2", "chorus", "chorus", harmony=True),
          S("bridge", "bridge", "bridge"),
          S("last chorus", "chorus", "chorus", harmony=True, last_chord="vi7"),
          S("tag", "tag", "tag", harmony=True)],
    craft=["the form: an intro (the hook on the keys), verse, pre-chorus, chorus, verse 2, chorus, a bridge, the last "
           "chorus, a tag", "7ths and 9ths ON the downbeats — lo-fi's colour, the 'luxury'",
           "verse lines 1 and 3 one shape; line 3 lands on the 7th of C7 — the yearning",
           "chorus: the title on a lazy rising third (D–F), the climax G5 on 'song' over Am7, once",
           "the bridge: Monday is coming — low, then climbing to 'it's getting late' on the 3rd of V7",
           "the last chorus ends on vi; the tag sings the title once more and lands home over V7 → I"],
)

# ── 3. Run With Me — progressive house / NCS anthem, A major, 126 bpm ──────────────────────────────────────────────
RUN_CHORUS = ["IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V", "I"]
RUN = dict(
    id="song-03", title="Run With Me", style="prog-house", style_label="Progressive house anthem (NCS)",
    tonic="A", mode="major", bpm=126, drums="four",
    story="Leaving the city at night with someone; by the bridge the sun is coming up and they keep driving into it.",
    parts=dict(
        verse=P(["vi7", "IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "Iadd9", "V"], [
            N(0, 0, .5, "C#5", "*ci", "i"), N(0, .5, .5, "C#5", "ty", "i"), N(0, 1, .5, "C#5", "*lights", "a"),
            N(0, 1.5, .5, "B4", "are", "a"), N(0, 2, .5, "C#5", "*fa", "e"), N(0, 2.5, .5, "B4", "ding", "i"),
            N(0, 3, .5, "A4", "*out", "a"), N(0, 3.5, .5, "F#4", "be", "i"), N(1, 0, 1, "A4", "*hind", "a"),
            N(1, 1, 2, "F#4", "us", "u"),
            N(2, 0, .5, "C#5", "*en", "e"), N(2, .5, .5, "C#5", "gine", "i"), N(2, 1, .5, "C#5", "*hum", "u"),
            N(2, 1.5, .5, "B4", "ming", "i"), N(2, 2, .5, "C#5", "like", "a"), N(2, 2.5, .5, "B4", "a", "a"),
            N(2, 3, .5, "E5", "*heart", "a"), N(2, 3.5, .5, "C#5", "beat", "i"), N(3, 0, .5, "B4", "in", "i"),
            N(3, .5, .5, "A4", "the", "e"), N(3, 1, 2.5, "G#4", "*dark", "a"),
            N(4, 0, .5, "C#5", "*ev", "e"), N(4, .5, .5, "C#5", "ery", "i"), N(4, 1, .5, "C#5", "*road", "o"),
            N(4, 1.5, .5, "B4", "we", "i"), N(4, 2, .5, "C#5", "*nev", "e"), N(4, 2.5, .5, "B4", "er", "e"),
            N(4, 3, .5, "A4", "*took", "u"), N(4, 3.5, .5, "F#4", "is", "i"), N(5, 0, 1, "A4", "*cal", "o"),
            N(5, 1, 2, "C#5", "ling", "i"),
            N(6, 0, .5, "A4", "and", "a"), N(6, .5, .5, "B4", "to", "u"), N(6, 1, 1, "C#5", "*night", "a"),
            N(6, 2, .5, "B4", "we're", "i"), N(6, 2.5, .5, "C#5", "gon", "a"), N(6, 3, .5, "D5", "na", "a"),
            N(6, 3.5, 1, "E5", "*leave", "i"), N(7, .5, .5, "D5", "a", "a"), N(7, 1, 2.5, "B4", "*mark", "a"),
        ], ["city lights are fading out behind us", "engine humming like a heartbeat in the dark",
            "every road we never took is calling", "and tonight we're gonna leave a mark"]),
        pre=P(["IVmaj7", "V", "vi7", "V"], [
            N(-1, 3.5, .5, "A4", "so", "o"), N(0, 0, .5, "D5", "*don't", "o"), N(0, .5, .5, "C#5", "*look", "u"),
            N(0, 1, 1, "A4", "*back", "a"), N(0, 2, .5, "D5", "*don't", "o"), N(0, 2.5, .5, "C#5", "look", "u"),
            N(0, 3, 1, "A4", "*back", "a"), N(1, 0, 2, "E5", "*now", "a"),
            N(2, 0, .5, "C#5", "the", "e"), N(2, .5, 1, "E5", "*sky", "a"), N(2, 1.5, .5, "D5", "is", "i"),
            N(2, 2, .5, "E5", "*o", "o"), N(2, 2.5, .5, "F#5", "pen", "e"), N(2, 3, 3, "E5", "ing", "i"),
        ], ["so don't look back, don't look back now", "the sky is opening"]),
        chorus=P(RUN_CHORUS, [
            N(0, 0, .75, "F#5", "*run", "u"), N(0, .75, .75, "E5", "with", "i"), N(0, 1.5, 2, "C#5", "*me", "i"),
            N(1, 0, .75, "E5", "*run", "u"), N(1, .75, .75, "C#5", "with", "i"), N(1, 1.5, 2, "A4", "*me", "i"),
            N(2, 0, .5, "B4", "till", "i"), N(2, .5, .5, "C#5", "the", "e"), N(2, 1, 1, "E5", "*mor", "o"),
            N(2, 2, .5, "D5", "ning", "i"), N(2, 2.5, 1, "C#5", "*breaks", "e"), N(2, 3.5, .5, "B4", "the", "e"),
            N(3, 0, 3, "C#5", "*night", "a"),
            N(4, 0, .75, "F#5", "*run", "u"), N(4, .75, .75, "E5", "with", "i"), N(4, 1.5, 2, "C#5", "*me", "i"),
            N(5, 0, .75, "A5", "*run", "u"), N(5, .75, .75, "F#5", "with", "i"), N(5, 1.5, 2, "E5", "*me", "i"),
            N(6, 0, .5, "B4", "we", "i"), N(6, .5, .5, "C#5", "were", "e"), N(6, 1, 1, "E5", "*made", "e"),
            N(6, 2, .5, "D5", "to", "u"), N(6, 2.5, 1, "B4", "*burn", "e"), N(6, 3.5, .5, "G#4", "this", "i"),
            N(7, 0, 4, "A4", "*bright", "a"),
        ], ["Run with me, run with me", "till the morning breaks the night",
            "run with me, run with me", "we were made to burn this bright"]),
        bridge=P(["vi7", "IVmaj7", "Iadd9", "V", "vi7", "IVmaj7", "IVmaj7", "V"], [
            N(-1, 3.5, .5, "C#5", "and", "a"), N(0, 0, 1, "C#5", "*if", "i"), N(0, 1, .5, "B4", "the", "e"),
            N(0, 1.5, 1, "A4", "*sun", "u"), N(0, 2.5, .5, "B4", "comes", "u"), N(0, 3, 1, "C#5", "*up", "u"),
            N(1, 0, .5, "D5", "too", "u"), N(1, .5, 2.5, "C#5", "*soon", "u"),
            N(1, 3.5, .5, "B4", "we'll", "i"), N(2, 0, 1, "C#5", "*dance", "a"), N(2, 1, .5, "B4", "be", "i"),
            N(2, 1.5, 1, "A4", "*neath", "i"), N(2, 2.5, .5, "G#4", "the", "e"), N(2, 3, 1, "A4", "*fad", "e"),
            N(3, 0, .5, "B4", "ing", "i"), N(3, .5, 2.5, "G#4", "*moon", "u"),
            N(3, 3.5, .5, "C#5", "and", "a"), N(4, 0, 1, "E5", "*ev", "e"), N(4, 1, .5, "D5", "ery", "i"),
            N(4, 1.5, 1, "C#5", "*star", "a"), N(4, 2.5, .5, "D5", "will", "i"), N(4, 3, 1, "E5", "*light", "a"),
            N(5, 0, .5, "F#5", "the", "e"), N(5, .5, 2.5, "E5", "*way", "e"),
            N(5, 3.5, .5, "E5", "so", "o"), N(6, 0, 1, "F#5", "*run", "u"), N(6, 1, .5, "E5", "with", "i"),
            N(6, 1.5, 1, "D5", "*me", "i"), N(6, 2.5, .5, "E5", "and", "a"), N(6, 3, 1, "F#5", "*chase", "e"),
            N(7, 0, .5, "D5", "the", "e"), N(7, .5, 2, "E5", "*day", "e"),
        ], ["and if the sun comes up too soon", "we'll dance beneath the fading moon",
            "and every star will light the way", "so run with me and chase the day"]),
        tag=P(["IVmaj7", "V", "Iadd9", "Iadd9"], [
            N(0, 0, .75, "F#5", "*run", "u"), N(0, .75, .75, "E5", "with", "i"), N(0, 1.5, 2, "C#5", "*me", "i"),
            N(1, 0, .5, "B4", "we", "i"), N(1, .5, .5, "C#5", "were", "e"), N(1, 1, 1, "E5", "*made", "e"),
            N(1, 2, .5, "D5", "to", "u"), N(1, 2.5, 1, "B4", "*burn", "e"), N(1, 3.5, .5, "G#4", "this", "i"),
            N(2, 0, 8, "A4", "*bright", "a"),
        ], ["run with me — we were made to burn this bright"]),
    ),
    form=[S("intro", "intro", hook=("chorus", 0, 4), chords=RUN_CHORUS[:4]),
          S("verse", "verse", "verse"), S("pre-chorus", "pre", "pre"), S("chorus", "chorus", "chorus"),
          S("drop", "drop", hook=("chorus", 0, 8), chords=RUN_CHORUS),
          S("verse 2", "verse", "verse", words=W(
              "*high:a way:e *stretch:e ing:i *out:a a:a *head:e for:o *ev:e er:e "
              "*ra:e dio:o *play:e ing:i all:o the:e *songs:o we:i used:u to:u *know:o "
              "*ev:e ery:i *mile:a we:i *drive:a we're:i *leav:i ing:i *some:u thing:i "
              "and:a to:u *night:a we're:i nev:e er:e *let:e ting:i *go:o"),
            lines=["highway stretching out ahead forever", "radio playing all the songs we used to know",
                   "every mile we drive we're leaving something", "and tonight we're never letting go"]),
          S("pre-chorus 2", "pre", "pre"), S("chorus 2", "chorus", "chorus", harmony=True),
          S("bridge", "bridge", "bridge"),
          S("last chorus", "chorus", "chorus", harmony=True, last_chord="vi7"),
          S("last drop", "drop", hook=("chorus", 0, 8), chords=RUN_CHORUS[:7] + ["vi7"]),
          S("tag", "tag", "tag", harmony=True)],
    craft=["the form: an intro teasing the hook, verse, pre-chorus, chorus, a drop, verse 2, chorus, a breakdown bridge, "
           "the last chorus, the last drop, a tag", "verse in running eighths, dwelling on C♯5 and falling",
           "pre-chorus: 'don't look back' twice, then up to F♯5, held over V",
           "chorus: the title as a one-bar hook (6-5-3), sequenced down a third at once; a minor-sixth leap to A5",
           "the bridge: the sun coming up — low and still, then climbing to 'chase the day', hanging on 5 over V",
           "the tag: the title once more and home, 7 → 1 over V → I, held"],
)

SONGS = [PAPER, SUNDAY, RUN]


def midi(name):
    names = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
    i, acc = 1, 0
    while i < len(name) and name[i] in '#b':
        acc += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + names[name[0]] + acc


def flatten(s):
    """The form strung out: absolute bars, chords, the sung notes (with words), the instrumental hook, the lyric."""
    bar, romans, sections, notes, hook, lyrics = 0, [], [], [], [], []
    for label, kind, part, o in s["form"]:
        p = s["parts"][part] if part else None
        rs = list(o.get("chords") or p["chords"])
        if o.get("last_chord"):
            rs[-1] = o["last_chord"]
        sections.append({"name": label, "kind": kind, "bar": bar, "bars": len(rs), "harmony": bool(o.get("harmony"))})
        if p:
            words = o.get("words")
            if words:
                sung = [n for n in p["notes"] if n[4] != "~"]
                assert len(words) == len(sung), f"{s['title']} {label}: {len(words)} words for {len(sung)} notes"
            k = 0
            for b, a, d, pitch, syl, v in p["notes"]:
                if words and syl != "~":
                    syl, v = words[k]
                    k += 1
                notes.append((4 * (bar + b) + a, d, pitch, syl, v))
            for ln in (o.get("lines") or p["lines"]):
                lyrics.append({"section": label, "line": ln})
        if o.get("hook"):
            src, b0, nb = o["hook"]
            for b, a, d, pitch, syl, v in s["parts"][src]["notes"]:
                if b0 <= b < b0 + nb:
                    hook.append((4 * (bar + b - b0) + a, d, pitch, syl, v))
        romans += rs
        bar += len(rs)
    notes.sort()
    # a held note running into the next line leaves a breath: a singer needs one
    for i in range(len(notes) - 1):
        t, d, *rest = notes[i]
        if d >= 3 and notes[i + 1][0] - (t + d) < 0.5:
            notes[i] = (t, max(1.5, notes[i + 1][0] - t - 1), *rest)
    return romans, sections, notes, sorted(hook), lyrics


def section_of(sections, bar):
    for sc in sections:
        if sc["bar"] <= bar < sc["bar"] + sc["bars"]:
            return sc
    return sections[-1]


def song_metrics(m):
    """The song's craft, measured — what references/songwriting.md asks of a verse, a pre-chorus and a chorus."""
    notes, pcs, roots, secs = m["notes"], m["chord_pcs"], m["chord_roots"], m["sections"]
    ps = [n["p"] for n in notes]
    ivs = [b - a for a, b in zip(ps, ps[1:])]
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
    top_kinds = sorted({section_of(secs, int(n["t"] // 4))["kind"] for n in notes if n["p"] == top})
    per_chorus = [sum(1 for n in notes if n["p"] == top and sc["bar"] <= n["t"] // 4 < sc["bar"] + sc["bars"])
                  for sc in secs if sc["kind"] == "chorus"]
    ci = ps.index(top)
    kind_mean = {}
    for kind in ("verse", "pre", "chorus", "bridge"):
        kn = [n["p"] for n in notes if section_of(secs, int(n["t"] // 4))["kind"] == kind]
        if kn:
            kind_mean[kind] = round(sum(kn) / len(kn), 1)
    misplaced = 0
    for a, b in zip(notes, notes[1:]):
        if b["t"] - (a["t"] + a["d"]) >= 0.49 and not (a["d"] >= 1.5 or b["t"] % 4 >= 2.5):
            misplaced += 1                                          # a rest that is not after a held note nor before a pickup
    last = notes[-1]
    chorus = next(sc for sc in secs if sc["kind"] == "chorus")
    hook = lambda bar: [(round(n["t"] - 4 * bar, 3), n["d"], n["p"]) for n in notes if bar <= n["t"] // 4 < bar + 1]
    name = lambda p: m["key_names"][p % 12] + str(p // 12 - 1)
    lb = int(last["t"] // 4)
    return {
        "notes": len(notes), "bars": len(pcs), "seconds": round(len(pcs) * 4 * 60 / m["bpm"]),
        "form": " · ".join(sc["name"] for sc in secs),
        "range": top - min(ps), "low": name(min(ps)), "high": name(top),
        "steps": round(sum(1 for iv in ivs if 1 <= abs(iv) <= 2) / len(ivs), 3),
        "repeats": round(sum(1 for iv in ivs if iv == 0) / len(ivs), 3),
        "leaps": sum(1 for iv in ivs if abs(iv) >= 5),
        "surprise_bits": round(surprise(ps), 3),
        "strong_chord_tones": round(strong_ct / strong, 3), "appoggiaturas": app,
        "stress_on_beat": round(stressed_ok / max(1, stressed), 3), "misstress": misstress,
        "climax_in": top_kinds, "climax_once_a_chorus": all(c == 1 for c in per_chorus),
        "climax_leap_in": ci > 0 and ps[ci] - ps[ci - 1] >= 3,
        "section_mean_pitch": kind_mean,
        "chorus_above_verse": kind_mean.get("chorus", 0) > kind_mean.get("verse", 0) + 2,
        "hook_returns": hook(chorus["bar"]) == hook(chorus["bar"] + 4),
        "misplaced_rests": misplaced,
        "ends_on_tonic": last["p"] % 12 == m["tonic"], "ends_on_downbeat": last["t"] % 4 == 0,
        "end_length": last["d"], "final_chord_tonic": roots[lb] == m["tonic"],
        "cadence": f"{m['chords'][lb - 1]} → {m['chords'][lb]}",
    }


def lab_melody(s):
    """A song as the Melody Lab and song.py read it: a chord a bar from bar 0, the sections with their kinds, the sung
    notes in beats with their syllables, stresses and vowels, the instrumental hook, and the lyric."""
    key = Key(s["tonic"], s["mode"])
    romans, sections, raw, hook, lyrics = flatten(s)
    chords = [chord(r, key) for r in romans]

    def note(t, d, pitch, syl, v):
        p = midi(pitch)
        return {"t": round(t, 4), "d": d, "p": p, "name": key.spell(p), "syl": syl.lstrip("*"),
                "stress": syl.startswith("*"), "vowel": VOWEL[v]}
    m = {"id": s["id"], "title": s["title"], "song": True, "style": s["style"], "style_label": s["style_label"],
         "bpm": s["bpm"], "key": key.name, "tonic": key.tonic, "mode": s["mode"], "key_names": key.names,
         "story": s["story"], "progression": romans, "chords": [c["name"] for c in chords],
         "chord_pcs": [c["pcs"] for c in chords], "chord_roots": [c["root"] for c in chords], "sections": sections,
         "lyrics": lyrics, "devices": s["craft"], "notes": [note(*n) for n in raw], "hook": [note(*n) for n in hook],
         "form": "song", "form_label": "song — the whole form", "drums": s["drums"], "voice": "sung",
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
        print(f"{m['id']} {m['title']} — {m['key']}, {m['bpm']} bpm, {k['bars']} bars ({k['seconds'] // 60}:"
              f"{k['seconds'] % 60:02d}), {k['notes']} notes\n   {k['form']}")
        print(f"   range {k['low']}–{k['high']} ({k['range']}) · steps {k['steps']} · repeats {k['repeats']} · "
              f"surprise {k['surprise_bits']} bits · strong chord tones {k['strong_chord_tones']} · leans {k['appoggiaturas']}")
        print(f"   stresses on the beat {k['stress_on_beat']} · misstressed {k['misstress']} · misplaced rests "
              f"{k['misplaced_rests']} · sections {k['section_mean_pitch']} · chorus above verse {k['chorus_above_verse']}")
        print(f"   climax {k['high']} in {k['climax_in']}, once a chorus {k['climax_once_a_chorus']}, leap in "
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
