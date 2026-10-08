# Solaris — the Arstro DAW

> Status: **second specification, being built** — see [`docs/PROGRESS.md`](docs/PROGRESS.md) for
> what exists today. Part of the [Arstro suite](../../docs/vision.md), beside Cosmo (photos) and
> Interstellar (video colour); same design family, teal accent.

Solaris arranges audio and notes on lanes, plays them through instruments and effects, mixes them
on as many mixer pages as you like, and sends the result to as many audio devices as you have.

**Its sound is the DSP library's.** Every instrument (Basic Synth, Drum Machine) and every effect
(Compressor, EQ, Reverb, Delay, Chorus, Drive, Filter) is a module of
[`core/DigitalSignalProcessing`](../../core/DigitalSignalProcessing), described by that library's
device registry. Solaris hosts them and routes them; it contains no DSP.

## The model in one paragraph

**Lanes hold time, strips hold sound.** A lane is a timeline row for organisation only. A strip is
one mixer line — every sample file and every instrument gets its own. Mixers are pages of strips in
an order (by default *Sources* → *Buses*, with a *Main* bus feeding the master), and routing only
goes forward, so a feedback loop cannot exist and the signal always reads left to right. A matrix
shows every route; every strip says what feeds it; `audit` lists what is unused or unreachable. The
project names **ports** ("Main", "Phones"); this machine's settings map them to devices — one
device is the clock and the others follow it, drift-corrected. Versions of a song (*Instrumental*,
*Extended*) are Interstellar's model: one shared sound, each version its base plus overrides.

How it came to be shaped like this: [`docs/discussion.md`](docs/discussion.md).

## Documents

| | |
|---|---|
| [`REQUIREMENTS.md`](REQUIREMENTS.md) | intent (R-) — what is asked and why |
| [`docs/requirements.md`](docs/requirements.md) | as built (DR-), with `file:line` anchors |
| [`docs/project-format.md`](docs/project-format.md) | the `.slp` text format and the command grammar |
| [`docs/architecture.md`](docs/architecture.md) | layers, module map, data flow, threading |
| [`docs/PROGRESS.md`](docs/PROGRESS.md) | the ledger — what is done, what is next |
| [`docs/DEFECTS.md`](docs/DEFECTS.md) | defects |
| [`docs/history/`](docs/history/) | the first specification, withdrawn |

Working on it: the `arstro.solaris.implement` skill.
