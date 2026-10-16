#include "AppModelCodec.h"
#include "Format.h"
#include "Notation.h"

namespace arstro
{
namespace solaris
{
    const std::vector<ModelField> &appModelFields()
    {
        static const std::vector<ModelField> f = {
            {"screen", "string", "home | project"},
            {"projectPath", "string", "the open .slp"},
            {"projectName", "string", "the song's name"},
            {"dirty", "bool", "unsaved changes"},
            {"bpm", "number", "tempo"},
            {"sig", "string", "meter, n/d"},
            {"sampleRate", "int", "the project's rate"},
            {"masterGain", "number", "dB"},
            {"masterGainFormula", "string", "the formula driving it, \"\" = none (R-AUTO-1)"},
            {"undoLabel", "string", "what `undo` would take back, \"\" = nothing (R-EDM-1)"},
            {"redoLabel", "string", "what `redo` would put back"},
            {"undoDepth", "int", "edits `undo` can take back"},
            {"redoDepth", "int", "steps `redo` can put back (down the newest branches)"},
            {"history", "object[]", "the song's history TREE, oldest first (R-HIST-2) — what `history` prints"},
            {"history[].id", "int", "never reused while the song is open; `history jump <id>`"},
            {"history[].parent", "int", "-1 at the root (the song as it was opened or made)"},
            {"history[].label", "string", "what made it: the command (`set ch_2.gain`), or New song / Opened"},
            {"historyCurrent", "int", "the node the song is now (-1: no song)"},
            {"recovery", "string", "the open song's path when it has an autosave newer than it — `project recover` restores it (R-SAFE-2)"},
            {"recoveryAge", "number", "how old that autosave is, seconds (-1: none)", false},
            {"masterOut", "string[]", "the port ids the master feeds"},
            {"mixers", "object[]", "mixer pages in order"},
            {"mixers[].id", "string", ""},
            {"mixers[].name", "string", ""},
            {"mixers[].order", "int", "processing and tab order"},
            {"mixers[].strips", "string[]", "its strips in processing order"},
            {"strips", "object[]", "every strip, in processing order"},
            {"strips[].id", "string", ""},
            {"strips[].name", "string", ""},
            {"strips[].kind", "string", "audio | instrument | bus"},
            {"strips[].mixer", "string", "the mixer it lives on (resolved)"},
            {"strips[].order", "int", "position on its mixer"},
            {"strips[].out", "string", "master, a strip id, or a port id (resolved)"},
            {"strips[].gain", "number", "dB"},
            {"strips[].pan", "number", "−1 … +1"},
            {"strips[].gainFormula", "string", "the formula driving the gain, \"\" = none (R-AUTO-1)"},
            {"strips[].panFormula", "string", "the formula driving the pan, \"\" = none"},
            {"strips[].mute", "bool", ""},
            {"strips[].solo", "bool", ""},
            {"strips[].input", "string", "R-REC-1: the input port it monitors and records; empty = none"},
            {"strips[].monitor", "string", "R-REC-1: auto (heard while armed and stopped or recording) | on | off"},
            {"strips[].audible", "bool", "false when muted or silenced by another strip's solo (R-MIX-7)"},
            {"strips[].colour", "int", "an index into the track colours: its own, else from its id — never −1"},
            {"strips[].sends", "object[]", "its sends"},
            {"strips[].sends[].id", "string", ""},
            {"strips[].sends[].to", "string", "a strip on a later mixer, master, or a port"},
            {"strips[].sends[].gain", "number", "dB"},
            {"strips[].sends[].pre", "bool", "pre-fader"},
            {"strips[].sends[].gainFormula", "string", "the formula driving the send's gain, \"\" = none"},
            {"strips[].sends[].sidechain", "bool", "a sidechain key: into the target's compressors' detectors, not its input (R-MIX-15)"},
            {"strips[].devices", "object[]", "the rack, in order (an instrument strip's first is its instrument)"},
            {"strips[].devices[].id", "string", ""},
            {"strips[].devices[].type", "string", "the DSP registry type"},
            {"strips[].devices[].label", "string", "the registry's label"},
            {"strips[].devices[].instrument", "bool", ""},
            {"strips[].devices[].bypass", "bool", ""},
            {"strips[].devices[].takesSample", "bool", "it plays a recorded sound (a sampler, R-EDM-8)"},
            {"strips[].devices[].sample", "string", "that sound, as stored (relative to the song); \"\" = none"},
            {"strips[].devices[].known", "bool", "false when this build's registry lacks the type (kept, not played)"},
            {"strips[].devices[].lastChanged", "string", "the parameter last written, by anyone (R-WIN-2)"},
            {"strips[].devices[].plugin", "string", "R-VST-6: type vst3 — another maker's plugin, its class id (its name is `label`); \"\" = a registry device"},
            {"strips[].devices[].offline", "bool", "R-VST-6: the plugin is not playing (never started, crashed, overran) — an effect passes dry, an instrument is silent"},
            {"strips[].devices[].why", "string", "R-VST-6: why it is offline (`device reload <dv>` starts it again)"},
            {"strips[].devices[].groups", "object[]", "R-UI-12: its parameter groups' titles, the registry's — `prefix` (before a parameter's dot, \"\" for none) and `label`"},
            {"strips[].devices[].groups[].prefix", "string", "the prefix before a parameter's dot; \"\" = the parameters with none"},
            {"strips[].devices[].groups[].label", "string", "its title, for a person"},
            {"strips[].devices[].params", "object[]", "every registry parameter, stored or default (R-UI-5)"},
            {"strips[].devices[].params[].name", "string", "the registry name, e.g. filter.cutoff"},
            {"strips[].devices[].params[].label", "string", ""},
            {"strips[].devices[].params[].unit", "string", "Hz, dB, ms, st, ct, oct, or empty"},
            {"strips[].devices[].params[].text", "string", "as stored: a number, or a choice's name"},
            {"strips[].devices[].params[].value", "number", "engineering units; a choice = its index"},
            {"strips[].devices[].params[].min", "number", ""},
            {"strips[].devices[].params[].max", "number", ""},
            {"strips[].devices[].params[].def", "number", ""},
            {"strips[].devices[].params[].choices", "string[]", "empty unless a choice"},
            {"strips[].devices[].params[].logScale", "bool", "a control's taper: moves in ratios (frequencies, times)"},
            {"strips[].devices[].params[].integer", "bool", "whole steps only"},
            {"strips[].devices[].params[].formula", "string", "the formula driving it, \"\" = its own value plays (R-AUTO-1)"},
            {"strips[].peak", "number[]", "the last played block's peaks, L/R (R-PLAY-3)", false},
            {"strips[].rms", "number[]", "RMS over the last 300 ms, L/R (R-METER-2)", false},
            {"strips[].hold", "number[]", "the held peak, L/R: the most of the last 1.5 s, then falling 20 dB a second", false},
            {"strips[].clip", "bool", "reached full scale since `meters clear` — a latch", false},
            {"strips[].clipCount", "int", "fed by: clips playing through it (R-MIX-8)"},
            {"strips[].fromLanes", "string[]", "fed by: the lanes those clips are drawn on"},
            {"strips[].fromStrips", "string[]", "fed by: strips whose output or a send lands here"},
            {"strips[].targets", "string[]", "where its output or a send may go (R-MIX-4): strips LATER in processing order (a later mixer, or after it on its own), master, output ports"},
            {"strips[].keyTargets", "string[]", "where a sidechain key may go (R-MIX-15): the strips of targets"},
            {"masterDevices", "object[]", "the master's rack, shaped like strips[].devices"},
            {"lanes", "object[]", "timeline rows, in order"},
            {"lanes[].id", "string", ""},
            {"lanes[].name", "string", ""},
            {"lanes[].order", "int", ""},
            {"lanes[].colour", "int", "−1 = none"},
            {"lanes[].strip", "string", "R-LANE-3: the instrument strip it is the track of — what is on it plays through it; empty = a plain lane"},
            {"lanes[].ownName", "bool", "false: a track showing its instrument strip's name (renaming the strip renames it)"},
            {"lanes[].group", "string", "R-ARR-3: the lane group it is in; empty = none"},
            {"groups", "object[]", "R-ARR-3: lane groups, in the order of their first lanes — each group's lanes side by side"},
            {"groups[].id", "string", "lg_n"},
            {"groups[].name", "string", ""},
            {"groups[].folded", "bool", "folded: its lanes are ONE row on the timeline"},
            {"groups[].lanes", "string[]", "its lanes, in order"},
            {"clips", "object[]", "every clip"},
            {"clips[].id", "string", ""},
            {"clips[].name", "string", ""},
            {"clips[].track", "string", "the strip it sounds through"},
            {"clips[].lane", "string", "the lane it is drawn on (\"\" = its strip's own row)"},
            {"clips[].kind", "string", "audio | note"},
            {"clips[].src", "string", "audio: the file"},
            {"clips[].pattern", "string", "note: the pattern it plays"},
            {"clips[].at", "number", "beats"},
            {"clips[].length", "number", "beats, resolved"},
            {"clips[].in", "number", "seconds into the file"},
            {"clips[].out", "number", "seconds into the file"},
            {"clips[].gain", "number", "dB"},
            {"clips[].fadeIn", "number", "beats"},
            {"clips[].fadeOut", "number", "beats"},
            {"clips[].loop", "bool", ""},
            {"clips[].offline", "bool", "the file could not be read"},
            {"clips[].linked", "int", "clips playing the same pattern, this one included (R-CLIP-3)"},
            {"clips[].takes", "string[]", "R-REC-2: a loop recording's passes, the files, in the order played (1-based in the comp)"},
            {"clips[].comp", "object[]", "R-REC-2: which take plays where — song beats [from, to); elsewhere the last take"},
            {"clips[].comp[].take", "int", "1-based"},
            {"clips[].comp[].from", "number", "beats"},
            {"clips[].comp[].to", "number", "beats"},
            {"clips[].plays", "object[]", "R-REC-2: the comp resolved — runs covering the clip, each the take heard there (the last where the comp says nothing)"},
            {"clips[].plays[].take", "int", ""},
            {"clips[].plays[].from", "number", "beats"},
            {"clips[].plays[].to", "number", "beats"},
            {"clips[].reverse", "bool", "R-EDIT-1: the span played backwards (audio)"},
            {"clips[].fadeInHeard", "number", "beats — R-EDIT-1: the fade in as heard: grown to cover an earlier clip overlapping it on its lane (a crossfade)"},
            {"clips[].fadeOutHeard", "number", "beats — R-EDIT-1: the fade out as heard: grown to cover a later clip overlapping it on its lane"},
            {"clips[].warp", "bool", "R-WARP-1: it follows the song's tempo — it lasts its span's own beats, stretched"},
            {"clips[].bpm", "number", "R-WARP-1: its own tempo (0: not known)"},
            {"clips[].pitch", "number", "semitones — R-WARP-1: its span moved, its length kept"},
            {"patterns", "object[]", "every pattern"},
            {"patterns[].id", "string", ""},
            {"patterns[].name", "string", ""},
            {"patterns[].length", "number", "beats"},
            {"patterns[].clips", "int", "clips playing it"},
            {"patterns[].strip", "string", "the strip its first clip plays through"},
            {"patterns[].instrument", "string", "that strip's instrument type (what names its keys)"},
            {"patterns[].strips", "string[]", "R-BROWSE-4: every strip a clip of it plays through, in clip order"},
            {"patterns[].lastStrip", "string", "the strip its newest clip plays through — where the MIDI list drops it off a track"},
            {"patterns[].notes", "object[]", "sorted by (at, pitch)"},
            {"patterns[].notes[].pitch", "int", "0…127"},
            {"patterns[].notes[].at", "number", "beats from the pattern's start"},
            {"patterns[].notes[].length", "number", "beats"},
            {"patterns[].notes[].vel", "int", "1…127"},
            {"patterns[].notes[].chance", "number", "R-ROLL-11: the odds it sounds in a repetition, 0 … 1 — rolled per clip and repetition by a hash"},
            {"patterns[].scale", "string", "R-ROLL-11: \"D dorian\" — placed and moved notes land on it; \"\" none"},
            {"patterns[].scaleMask", "int", "its pitch classes: bit k for pitch class k (C = 0); 0 none"},
            {"patterns[].expr", "object[]", "R-MIDI-3: expression lanes' points, sorted by (lane, at) — linear between, held at the ends"},
            {"patterns[].expr[].lane", "string", "bend (−1 … +1) | cc0 … cc127 (0 … 1; cc1 the mod wheel)"},
            {"patterns[].expr[].at", "number", "beats from the pattern's start"},
            {"patterns[].expr[].value", "number", "bend −1 … +1, a controller 0 … 1"},
            {"markers", "object[]", "R-ARR-1: named places in the song, by time"},
            {"markers[].id", "string", "mk_n"},
            {"markers[].name", "string", ""},
            {"markers[].note", "string", "free text (Interstellar's marker note)"},
            {"markers[].at", "number", "beats"},
            {"markers[].length", "number", "beats — a section; 0 a point"},
            {"markers[].end", "number", "where its section ends: at + length, else the next marker, else the song's end"},
            {"tempos", "object[]", "R-TEMPO-1: the tempo map's changes, by time — before the first, `bpm`"},
            {"tempos[].id", "string", "tp_n"},
            {"tempos[].at", "number", "beats"},
            {"tempos[].bpm", "number", "beats a minute from there"},
            {"tempos[].ramp", "bool", "reached gliding (linearly in beats) from the tempo before"},
            {"tempos[].seconds", "number", "where it is heard (computed through the map)"},
            {"presets", "object[]", "R-PRESET-1: every device preset — yours, then the factory's of a name you have not — by type, then name"},
            {"presets[].type", "string", "the registry type it is for"},
            {"presets[].name", "string", ""},
            {"presets[].factory", "bool", "shipped with the app (never written or deleted)"},
            {"templates", "object[]", "R-PRESET-2: every song template — yours, then the factory's of a name you have not — by name"},
            {"templates[].name", "string", "what `project new --template` takes"},
            {"templates[].factory", "bool", "shipped with the app (never written or deleted)"},
            {"automations", "object[]", "every automation (R-AUTO-4)"},
            {"automations[].id", "string", "au_n — what a formula names"},
            {"automations[].name", "string", "\"<owner> · <parameter>\" when made from one"},
            {"automations[].unit", "string", ""},
            {"automations[].from", "string", "the address it was made from (a hint, not a link)"},
            {"automations[].min", "number", ""},
            {"automations[].max", "number", ""},
            {"automations[].loop", "number", "beats (R-AUTO-12): its first `loop` beats repeat — 0: it does not loop"},
            {"automations[].at", "number", "beats: where the repeating starts"},
            {"automations[].length", "number", "beats it repeats for — 0: to the song's end; outside, where a cycle ends"},
            {"automations[].points", "object[]", "sorted by at"},
            {"automations[].points[].at", "number", "beats"},
            {"automations[].points[].value", "number", "in the automation's unit"},
            {"automations[].points[].shape", "string", "linear | hold | smooth | bezier — the segment after it (bezier: both its sides)"},
            {"automations[].points[].speedIn", "number", "bezier (R-AUTO-10): the slope arriving, in the automation's unit per beat"},
            {"automations[].points[].inflIn", "number", "bezier: how far into the segment before it that slope pulls, % (0 < x ≤ 100)"},
            {"automations[].points[].speedOut", "number", "bezier: the slope leaving, unit per beat"},
            {"automations[].points[].inflOut", "number", "bezier: how far into the segment after it, %"},
            {"automations[].usedBy", "string[]", "addresses whose formula reads it"},
            {"automations[].now", "number", "its value at the playhead (`transport.position`) by the engine's own evaluator; follows the "
                                            "transport while playing (R-AUTO-11)", false},
            {"bindings", "object[]", "every formula (R-AUTO-1/9)"},
            {"bindings[].address", "string", "what it drives"},
            {"bindings[].formula", "string", "as typed, with its leading ="},
            {"bindings[].reads", "string[]", "the automations and addresses it reads"},
            {"bindings[].ok", "bool", "false = INERT: the address's own value plays"},
            {"bindings[].problem", "string", "why it is inert"},
            {"bindings[].live", "number", "its value at the HEARD position (R-MIX-16): the engine's while playing, eval's at the transport when stopped", false},
            {"ports", "object[]", "logical ports (R-DEV-3)"},
            {"ports[].id", "string", ""},
            {"ports[].name", "string", ""},
            {"ports[].dir", "string", "in | out"},
            {"ports[].channels", "int", ""},
            {"matrix", "object", "the routing matrix (R-MIX-9), computed once for every face"},
            {"matrix.columns", "string[]", "its columns: every strip some strip may or does reach (processing order), \"master\", the output ports — every route and key has a cell"},
            {"lengthBeats", "number", "where the last clip ends"},
            {"audit", "string[]", "the last `audit`'s findings"},
            {"lastError", "string", "the last refusal or failure"},
            {"recents", "object[]", "Home's cards, newest first (R-HOME-1)"},
            {"recents[].path", "string", ""},
            {"recents[].name", "string", ""},
            {"recents[].bpm", "number", ""},
            {"recents[].lengthBeats", "number", ""},
            {"recents[].strips", "int", ""},
            {"recents[].missing", "bool", "the file is gone or no longer a song"},
            {"recents[].recovery", "bool", "an autosave newer than it: unsaved edits to recover (R-SAFE-2)"},
            {"settings", "object", "the MACHINE's settings (R-SET-2)"},
            {"settings.sampleRate", "int", "new songs, and the clock device's rate"},
            {"settings.bufferSize", "int", "frames per device write"},
            {"settings.latencyMs", "number", "what that buffer costs at that rate"},
            {"settings.output", "string", "the clock device ('' = the system default)"},
            {"settings.input", "string", ""},
            {"settings.folders", "string[]", "the browser's quick-access folders, in order"},
            {"settings.ports", "string[]", "<port>=<device>:<channel> on this machine"},
            {"settings.metronome", "bool", "clicks while playing (R-TIME-4), never in a render"},
            {"settings.metronomeLevel", "number", "dB"},
            {"settings.auditionLevel", "number", "dB: a sample previewed from the browser (R-EDM-9)"},
            {"settings.newBpm", "number", "a new song's tempo without --bpm"},
            {"settings.newSig", "string", "a new song's meter without --sig"},
            {"settings.reducedMotion", "bool", "the UI's tweens collapse (with the OS's own setting)"},
            {"settings.showIds", "bool", "every object's id drawn beside its name (R-UI-11)"},
            {"settings.autosave", "int", "seconds between autosaves of an unsaved song; 0 = off (R-SAFE-1)"},
            {"settings.driver", "string", "pulse | pipewire: how the clock device is reached (R-DEV-9)"},
            {"settings.recordQuantize", "string", "off | 1/4 | 1/8 | 1/16 | 1/32: where a recorded note's start lands (R-MIDI-2)"},
            {"settings.countIn", "int", "bars of click before a recording, 0 … 4 (R-MIDI-2)"},
            {"settings.midiInputs", "string", "all | none | MIDI port names, comma-separated: the keyboards Solaris listens to (R-MIDI-2)"},
            {"armed", "string", "the strip the MIDI keyboard plays ('' none) — the session's, never saved (R-MIDI-2)"},
            {"midiDevices", "object[]", "this machine's MIDI inputs, as `midi devices` last listed them (R-MIDI-2)"},
            {"midiDevices[].id", "string", "what `settings set midiInputs=` names"},
            {"midiDevices[].name", "string", ""},
            {"midiDevices[].listening", "bool", "Solaris is subscribed to it"},
            {"midiMessages", "int", "messages the keyboard sent since start — an activity light", false},
            {"settings.keys", "object[]", "every keyboard action (R-SET-4), in the sheet's order — a pressed combo is looked up here"},
            {"settings.keys[].id", "string", "transport.play … — set with `settings set key.<id>=<combos>`"},
            {"settings.keys[].area", "string", "Transport | Song | Lanes | Piano roll (the first three are one keyboard)"},
            {"settings.keys[].label", "string", ""},
            {"settings.keys[].keys", "string[]", "its combos on this machine, canonical (Ctrl+Alt+Shift+<key>); empty = no key"},
            {"settings.keys[].defaults", "string[]", "what `default` gives back"},
            {"devices", "object[]", "from the last `devices list`"},
            {"devices[].id", "string", "what `settings set output=` takes"},
            {"devices[].name", "string", ""},
            {"devices[].dir", "string", "out | in"},
            {"devices[].channels", "int", ""},
            {"devices[].rate", "int", ""},
            {"transport", "object", "play, position, loop (R-TIME-4)"},
            {"transport.playing", "bool", ""},
            {"transport.position", "number", "beats — what is heard", false},
            {"transport.tempo", "number", "R-TEMPO-1: the tempo where the time bar is (bpm)", false},
            {"transport.tempoId", "string", "R-TEMPO-1: the change in effect there — tp_n, or '' before the first (the song's bpm)", false},
            {"transport.loopFrom", "number", "beats"},
            {"transport.loopTo", "number", "beats; equal to loopFrom = no loop"},
            {"transport.punchFrom", "number", "R-REC-1: beats — a recording keeps only from here … (the session's)"},
            {"transport.punchTo", "number", "… to here; equal to punchFrom = all of it"},
            {"transport.cue", "number", "beats: where the time bar was last put (a seek, or a play's --from / --clip; never a plain play) — `transport stop --return` goes back there (R-TIME-7)"},
            {"transport.loopClip", "string", "the clip the loop follows (`transport play --clip`, the piano roll's play); '' = the song's own loop"},
            {"transport.latencyMs", "number", "the clock device's", false},
            {"transport.device", "string", "the clock device ('' = the system default)"},
            {"transport.masterPeak", "number[]", "L/R peaks of the last played block", false},
            {"transport.masterRms", "number[]", "the master's RMS over 300 ms, L/R (R-METER-2)", false},
            {"transport.masterHold", "number[]", "the master's held peak, L/R", false},
            {"transport.masterClip", "bool", "the master reached full scale since `meters clear`", false},
            {"transport.driver", "string", "what the clock device plays through now: pulse | pipewire ('' stopped) — R-DEV-9", false},
            {"transport.recording", "bool", "`transport record` is keeping the armed strip's keyboard (R-MIDI-2)"},
            {"transport.countingIn", "bool", "… and its count-in still clicks", false},
            {"transport.recorded", "int", "notes struck that will land (a count-in's are not)", false},
            {"transport.xruns", "int", "underruns since play (−1: this driver cannot count them) — R-DEV-5", false},
            {"loudness", "object", "the master as measured while playing, kept after a stop (R-METER-1, ITU-R BS.1770-4)", false},
            {"loudness.momentary", "number", "LUFS over the last 400 ms; −144 = nothing measured", false},
            {"loudness.shortTerm", "number", "LUFS over the last 3 s", false},
            {"loudness.integrated", "number", "LUFS, gated (−70 absolute, −10 LU relative), since play or `meters clear`", false},
            {"loudness.truePeak", "number", "dBTP (4× oversampled), the most since play or `meters clear`", false},
            {"loudness.correlation", "number", "stereo correlation over 300 ms, −1 … +1 (0: nothing to compare)", false},
            {"loudness.spectrum", "number[]", "61 bands of 1/6 octave, dB re a full-scale sine: band b's centre is 1000·2^((b−34)/6) Hz", false},
            {"audition", "object", "a sample heard from the browser, outside the song (R-EDM-9)"},
            {"audition.file", "string", "the file previewed ('' = none)"},
            {"audition.playing", "bool", ""},
            {"audition.progress", "number", "0 … 1 of the file handed to the device", false},
            {"deviceTypes", "object[]", "the DSP registry: every instrument and effect a strip can host (R-BROWSE-1)"},
            {"deviceTypes[].name", "string", "the registry type, what `device add --type` and `--instrument` take"},
            {"deviceTypes[].label", "string", ""},
            {"deviceTypes[].kind", "string", "instrument | effect"},
            {"deviceTypes[].noteNames", "object[]", "a kit's keys, named (REQ-device-6)"},
            {"deviceTypes[].takesSample", "bool", "it plays a recorded sound: give it one with --sample / <dv>.sample (R-EDM-8)"},
            {"deviceTypes[].noteNames[].note", "int", ""},
            {"deviceTypes[].noteNames[].name", "string", ""},
            {"chords", "object[]", "R-ROLL-11: the qualities `note add --chord <root><name>` takes"},
            {"chords[].name", "string", "\"\" (major), m, 7, maj7, m7 …"},
            {"chords[].label", "string", ""},
            {"scales", "object[]", "R-ROLL-11: the scales `pattern scale <pt> <key> <name>` takes"},
            {"scales[].name", "string", ""},
            {"scales[].label", "string", ""},
            {"grooves", "object[]", "R-ROLL-11: the templates `pattern quantize --groove <name>` takes"},
            {"grooves[].name", "string", ""},
            {"grooves[].label", "string", ""},
            {"browser", "object", "the folder last browsed"},
            {"browser.path", "string", ""},
            {"browser.entries", "object[]", "folders first, then by name"},
            {"browser.entries[].name", "string", ""},
            {"browser.entries[].path", "string", ""},
            {"browser.entries[].kind", "string", "dir | audio | song | other"},
            {"browser.search", "string", "R-PRESET-3: what the entries were searched for ('' a folder listed; `favourites`)"},
            {"library", "object[]", "R-PRESET-3: every tagged sound in the machine's library, by path"},
            {"library[].path", "string", "absolute"},
            {"library[].tags", "string[]", "lower case, sorted — `favourite` not among them"},
            {"library[].favourite", "bool", ""},
            {"plugins", "object[]", "R-VST-6: the VST3 plugins the last scan found (the machine's)"},
            {"plugins[].id", "string", "the class's UID, 32 hex digits — what `device add --plugin` takes"},
            {"plugins[].name", "string", ""},
            {"plugins[].vendor", "string", ""},
            {"plugins[].kind", "string", "instrument | effect"},
            {"plugins[].path", "string", "its bundle"},
            {"revision", "int", "bumps on every change", false},
        };
        return f;
    }

    namespace
    {
        Json strings(const std::vector<std::string> &v)
        {
            Json a = Json::array();
            for (const auto &s : v) a.push(Json::string(s));
            return a;
        }
        Json groupsOf(const std::vector<ParamGroupModel> &gs)
        {
            Json a = Json::array();
            for (const auto &g : gs) a.push(Json::object().set("prefix", g.prefix).set("label", g.label));
            return a;
        }
        Json devices(const std::vector<DeviceModel> &ds)
        {
            Json a = Json::array();
            for (const auto &d : ds)
            {
                Json params = Json::array();
                for (const auto &p : d.params)
                    params.push(Json::object()
                                    .set("name", p.name).set("label", p.label).set("unit", p.unit).set("text", p.text)
                                    .set("value", p.value).set("min", p.min).set("max", p.max).set("def", p.def)
                                    .set("choices", strings(p.choices)).set("logScale", p.logScale).set("integer", p.integer)
                                    .set("formula", p.formula));
                a.push(Json::object().set("id", d.id).set("type", d.type).set("label", d.label)
                           .set("instrument", d.instrument).set("bypass", d.bypass).set("known", d.known).set("lastChanged", d.lastChanged)
                           .set("takesSample", d.takesSample).set("sample", d.sample)
                           .set("plugin", d.plugin).set("offline", d.offline).set("why", d.why)
                           .set("groups", groupsOf(d.groups))
                           .set("params", params));
            }
            return a;
        }
    }

    namespace
    {
        // R-SVC-8: `state print --json --compact` — the SONG for an agent's context: no registry, no machine, no
        // recents or browser; a device's non-default parameters as pasteable `name=value`; notes in the notation
        Json compactDevices(const std::vector<DeviceModel> &ds)
        {
            Json a = Json::array();
            for (const auto &d : ds)
            {
                Json params = Json::array();
                for (const auto &p : d.params)
                    if (!p.formula.empty()) params.push(Json::string(p.name + "=" + p.formula));
                    else if (!d.known || p.value != p.def) params.push(Json::string(p.name + "=" + p.text));
                Json j = Json::object();
                j.set("id", d.id).set("type", d.type);
                if (d.bypass) j.set("bypass", true);
                if (!d.sample.empty()) j.set("sample", d.sample);
                if (!d.plugin.empty()) j.set("plugin", d.plugin).set("label", d.label); // R-VST-6
                if (d.offline) j.set("offline", d.why);
                a.push(j.set("params", params));
            }
            return a;
        }
        std::string beatText(double b)
        {
            std::string s = canonicalBeats(b);
            if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
            return s;
        }
        Json compactJson(const AppModel &m)
        {
            Json j = Json::object();
            j.set("projectPath", m.projectPath).set("projectName", m.projectName).set("dirty", m.dirty).set("bpm", m.bpm)
                .set("sig", m.sig).set("sampleRate", m.sampleRate).set("lengthBeats", m.lengthBeats);
            if (m.masterGain != 0) j.set("masterGain", m.masterGain);
            if (!m.masterGainFormula.empty()) j.set("masterGainFormula", m.masterGainFormula);
            j.set("masterOut", strings(m.masterOut));
            Json mixers = Json::array();
            for (const auto &x : m.mixers) mixers.push(Json::object().set("id", x.id).set("name", x.name).set("strips", strings(x.strips)));
            j.set("mixers", mixers);
            Json strips = Json::array();
            for (const auto &s : m.strips)
            {
                Json st = Json::object();
                st.set("id", s.id).set("name", s.name).set("kind", s.kind).set("out", s.out);
                if (s.gain != 0) st.set("gain", s.gain);
                if (s.pan != 0) st.set("pan", s.pan);
                if (!s.gainFormula.empty()) st.set("gainFormula", s.gainFormula);
                if (!s.panFormula.empty()) st.set("panFormula", s.panFormula);
                if (s.mute) st.set("mute", true);
                if (s.solo) st.set("solo", true);
                if (!s.sends.empty())
                {
                    Json sends = Json::array();
                    for (const auto &sd : s.sends)
                    {
                        Json x = Json::object();
                        x.set("id", sd.id).set("to", sd.to).set("gain", sd.gain);
                        if (sd.pre) x.set("pre", true);
                        if (sd.sidechain) x.set("sidechain", true);
                        if (!sd.gainFormula.empty()) x.set("gainFormula", sd.gainFormula);
                        sends.push(x);
                    }
                    st.set("sends", sends);
                }
                if (!s.devices.empty()) st.set("devices", compactDevices(s.devices));
                strips.push(st);
            }
            j.set("strips", strips);
            if (!m.masterDevices.empty()) j.set("masterDevices", compactDevices(m.masterDevices));
            Json lanes = Json::array();
            for (const auto &l : m.lanes)
            {
                Json x = Json::object().set("id", l.id).set("name", l.name);
                if (!l.strip.empty()) x.set("track", l.strip);
                if (!l.group.empty()) x.set("group", l.group); // R-ARR-3
                lanes.push(x);
            }
            j.set("lanes", lanes);
            if (!m.groups.empty())
            {
                Json gs = Json::array(); // R-ARR-3: "<id> <name>[ folded]"
                for (const auto &g : m.groups) gs.push(Json::string(g.id + " " + g.name + (g.folded ? " folded" : "")));
                j.set("groups", gs);
            }
            Json clips = Json::array();
            for (const auto &c : m.clips)
            {
                Json x = Json::object();
                x.set("id", c.id).set("track", c.track).set("lane", c.lane).set("at", c.at).set("length", c.length);
                if (c.kind == "audio") x.set("src", c.src);
                else x.set("pattern", c.pattern);
                if (c.gain != 0) x.set("gain", c.gain);
                if (c.fadeIn != 0) x.set("fadeIn", c.fadeIn);
                if (c.fadeOut != 0) x.set("fadeOut", c.fadeOut);
                if (c.offline) x.set("offline", true);
                clips.push(x);
            }
            j.set("clips", clips);
            Json patterns = Json::array();
            for (const auto &p : m.patterns)
            {
                Json notes = Json::array();
                for (const auto &n : p.notes)
                    notes.push(Json::string(pitchName(n.pitch) + "@" + beatText(n.at) + ":" + beatText(n.length) + ":" + std::to_string(n.vel) +
                                            (n.chance < 1 ? ":" + canonicalNumber(n.chance) : std::string()))); // R-ROLL-11
                Json pj = Json::object().set("id", p.id).set("name", p.name).set("length", p.length).set("clips", p.clips).set("notes", notes);
                if (!p.scale.empty()) pj.set("scale", p.scale);
                if (!p.expr.empty())
                {
                    Json ex = Json::array(); // R-MIDI-3: "<lane>@<beat>=<value>"
                    for (const auto &x : p.expr) ex.push(Json::string(x.lane + "@" + beatText(x.at) + "=" + canonicalNumber(x.value)));
                    pj.set("expr", ex);
                }
                patterns.push(pj);
            }
            j.set("patterns", patterns);
            if (!m.markers.empty())
            {
                Json mk = Json::array(); // R-ARR-1: "<id> <name>@<at>..<end>"
                for (const auto &x : m.markers) mk.push(Json::string(x.id + " " + x.name + "@" + beatText(x.at) + ".." + beatText(x.end)));
                j.set("markers", mk);
            }
            if (!m.automations.empty())
            {
                Json autos = Json::array();
                for (const auto &a : m.automations)
                {
                    Json pts = Json::array();
                    for (const auto &pt : a.points) pts.push(Json::string(beatText(pt.at) + "=" + canonicalNumber(pt.value) + (pt.shape == "linear" ? "" : " " + pt.shape)));
                    Json ao = Json::object().set("id", a.id).set("name", a.name).set("min", a.min).set("max", a.max).set("points", pts)
                                  .set("usedBy", strings(a.usedBy));
                    if (a.loop > 0) ao.set("loop", a.loop).set("at", a.at).set("length", a.length); // R-AUTO-12
                    autos.push(ao);
                }
                j.set("automations", autos);
            }
            if (!m.bindings.empty())
            {
                Json binds = Json::array();
                for (const auto &b : m.bindings)
                {
                    Json x = Json::object();
                    x.set("address", b.address).set("formula", b.formula);
                    if (!b.ok) x.set("problem", b.problem);
                    binds.push(x);
                }
                j.set("bindings", binds);
            }
            if (!m.audit.empty()) j.set("audit", strings(m.audit));
            if (!m.lastError.empty()) j.set("lastError", m.lastError);
            return j;
        }
    }

    Json modelToJson(const AppModel &m, bool stable, bool compact)
    {
        if (compact) return compactJson(m);
        Json j = Json::object();
        j.set("screen", m.screen).set("projectPath", m.projectPath).set("projectName", m.projectName).set("dirty", m.dirty)
            .set("bpm", m.bpm).set("sig", m.sig).set("sampleRate", m.sampleRate).set("masterGain", m.masterGain)
            .set("masterGainFormula", m.masterGainFormula).set("undoLabel", m.undoLabel).set("redoLabel", m.redoLabel)
            .set("undoDepth", m.undoDepth).set("redoDepth", m.redoDepth).set("masterOut", strings(m.masterOut));
        {
            Json h = Json::array();
            for (const auto &n : m.history) h.push(Json::object().set("id", n.id).set("parent", n.parent).set("label", n.label));
            j.set("history", h).set("historyCurrent", m.historyCurrent).set("recovery", m.recovery);
            if (!stable) j.set("recoveryAge", m.recoveryAge);
        }
        Json mixers = Json::array();
        for (const auto &x : m.mixers)
            mixers.push(Json::object().set("id", x.id).set("name", x.name).set("order", x.order).set("strips", strings(x.strips)));
        j.set("mixers", mixers);
        Json strips = Json::array();
        for (const auto &s : m.strips)
        {
            Json sends = Json::array();
            for (const auto &sd : s.sends)
                sends.push(Json::object().set("id", sd.id).set("to", sd.to).set("gain", sd.gain).set("pre", sd.pre).set("gainFormula", sd.gainFormula).set("sidechain", sd.sidechain));
            Json st = Json::object();
            st.set("id", s.id).set("name", s.name).set("kind", s.kind).set("mixer", s.mixer).set("order", s.order)
                            .set("out", s.out).set("gain", s.gain).set("pan", s.pan).set("gainFormula", s.gainFormula)
                            .set("panFormula", s.panFormula).set("mute", s.mute).set("solo", s.solo).set("input", s.input).set("monitor", s.monitor)
                            .set("audible", s.audible).set("colour", s.colour).set("sends", sends).set("devices", devices(s.devices));
            if (!stable)
                st.set("peak", Json::array().push(Json::number(s.peak[0])).push(Json::number(s.peak[1])))
                    .set("rms", Json::array().push(Json::number(s.rms[0])).push(Json::number(s.rms[1])))
                    .set("hold", Json::array().push(Json::number(s.hold[0])).push(Json::number(s.hold[1])))
                    .set("clip", s.clip);
            st.set("clipCount", s.clipCount).set("fromLanes", strings(s.fromLanes)).set("fromStrips", strings(s.fromStrips))
                .set("targets", strings(s.targets)).set("keyTargets", strings(s.keyTargets));
            strips.push(st);
        }
        j.set("strips", strips);
        j.set("masterDevices", devices(m.masterDevices));
        Json lanes = Json::array();
        for (const auto &l : m.lanes)
            lanes.push(Json::object().set("id", l.id).set("name", l.name).set("order", l.order).set("colour", l.colour).set("strip", l.strip).set("ownName", l.ownName)
                           .set("group", l.group));
        j.set("lanes", lanes);
        Json groups = Json::array(); // R-ARR-3
        for (const auto &g : m.groups) groups.push(Json::object().set("id", g.id).set("name", g.name).set("folded", g.folded).set("lanes", strings(g.lanes)));
        j.set("groups", groups);
        Json clips = Json::array();
        for (const auto &c : m.clips)
        {
            Json comp = Json::array(); // R-REC-2
            for (const auto &x : c.comp) comp.push(Json::object().set("take", x.take).set("from", x.from).set("to", x.to));
            Json plays = Json::array();
            for (const auto &x : c.plays) plays.push(Json::object().set("take", x.take).set("from", x.from).set("to", x.to));
            clips.push(Json::object()
                           .set("id", c.id).set("name", c.name).set("track", c.track).set("lane", c.lane).set("kind", c.kind)
                           .set("src", c.src).set("pattern", c.pattern).set("at", c.at).set("length", c.length).set("in", c.in)
                           .set("out", c.out).set("gain", c.gain).set("fadeIn", c.fadeIn).set("fadeOut", c.fadeOut)
                           .set("loop", c.loop).set("offline", c.offline).set("linked", c.linked).set("takes", strings(c.takes)).set("comp", comp)
                           .set("plays", plays).set("reverse", c.reverse).set("fadeInHeard", c.fadeInHeard).set("fadeOutHeard", c.fadeOutHeard)
                           .set("warp", c.warp).set("bpm", c.bpm).set("pitch", c.pitch));
        }
        j.set("clips", clips);
        Json patterns = Json::array();
        for (const auto &p : m.patterns)
        {
            Json notes = Json::array();
            for (const auto &n : p.notes)
                notes.push(Json::object().set("pitch", n.pitch).set("at", n.at).set("length", n.length).set("vel", n.vel).set("chance", n.chance));
            Json ex = Json::array();
            for (const auto &x : p.expr) ex.push(Json::object().set("lane", x.lane).set("at", x.at).set("value", x.value));
            patterns.push(Json::object().set("id", p.id).set("name", p.name).set("length", p.length).set("clips", p.clips).set("notes", notes)
                              .set("expr", ex).set("strip", p.strip).set("instrument", p.instrument).set("strips", strings(p.strips)).set("lastStrip", p.lastStrip)
                              .set("scale", p.scale).set("scaleMask", p.scaleMask));
        }
        j.set("patterns", patterns);
        Json markers = Json::array(); // R-ARR-1
        for (const auto &x : m.markers)
            markers.push(Json::object().set("id", x.id).set("name", x.name).set("note", x.note).set("at", x.at).set("length", x.length).set("end", x.end));
        j.set("markers", markers);
        Json tempos = Json::array(); // R-TEMPO-1
        for (const auto &x : m.tempos) tempos.push(Json::object().set("id", x.id).set("at", x.at).set("bpm", x.bpm).set("ramp", x.ramp).set("seconds", x.seconds));
        j.set("tempos", tempos);
        Json presets = Json::array(); // R-PRESET-1
        for (const auto &x : m.presets) presets.push(Json::object().set("type", x.type).set("name", x.name).set("factory", x.factory));
        j.set("presets", presets);
        Json templates = Json::array(); // R-PRESET-2
        for (const auto &x : m.templates) templates.push(Json::object().set("name", x.name).set("factory", x.factory));
        j.set("templates", templates);
        Json autos = Json::array();
        for (const auto &a : m.automations)
        {
            Json pts = Json::array();
            for (const auto &pt : a.points)
                pts.push(Json::object().set("at", pt.at).set("value", pt.value).set("shape", pt.shape).set("speedIn", pt.speedIn)
                             .set("inflIn", pt.inflIn).set("speedOut", pt.speedOut).set("inflOut", pt.inflOut));
            Json ao = Json::object().set("id", a.id).set("name", a.name).set("unit", a.unit).set("from", a.from).set("min", a.min)
                          .set("max", a.max).set("loop", a.loop).set("at", a.at).set("length", a.length).set("points", pts)
                          .set("usedBy", strings(a.usedBy));
            if (!stable) ao.set("now", a.now);
            autos.push(ao);
        }
        j.set("automations", autos);
        Json binds = Json::array();
        for (const auto &b : m.bindings)
        {
            Json bj = Json::object();
            bj.set("address", b.address).set("formula", b.formula).set("reads", strings(b.reads)).set("ok", b.ok).set("problem", b.problem);
            if (!stable) bj.set("live", b.live); // it moves with the transport
            binds.push(bj);
        }
        j.set("bindings", binds);
        Json ports = Json::array();
        for (const auto &p : m.ports) ports.push(Json::object().set("id", p.id).set("name", p.name).set("dir", p.dir).set("channels", p.channels));
        j.set("ports", ports);
        j.set("matrix", Json::object().set("columns", strings(m.matrix.columns)));
        j.set("lengthBeats", m.lengthBeats).set("audit", strings(m.audit)).set("lastError", m.lastError);
        Json recents = Json::array();
        for (const auto &r : m.recents)
            recents.push(Json::object().set("path", r.path).set("name", r.name).set("bpm", r.bpm).set("lengthBeats", r.lengthBeats)
                             .set("strips", r.strips).set("missing", r.missing).set("recovery", r.recovery));
        j.set("recents", recents);
        Json keys = Json::array();
        for (const auto &k : m.settings.keys)
            keys.push(Json::object().set("id", k.id).set("area", k.area).set("label", k.label).set("keys", strings(k.keys)).set("defaults", strings(k.defaults)));
        j.set("settings", Json::object().set("sampleRate", m.settings.sampleRate).set("bufferSize", m.settings.bufferSize)
                              .set("latencyMs", m.settings.latencyMs).set("output", m.settings.output).set("input", m.settings.input)
                              .set("folders", strings(m.settings.folders)).set("ports", strings(m.settings.ports))
                              .set("metronome", m.settings.metronome).set("metronomeLevel", m.settings.metronomeLevel).set("auditionLevel", m.settings.auditionLevel)
                              .set("newBpm", m.settings.newBpm).set("newSig", m.settings.newSig).set("reducedMotion", m.settings.reducedMotion)
                              .set("showIds", m.settings.showIds).set("autosave", m.settings.autosave).set("driver", m.settings.driver).set("midiInputs", m.settings.midiInputs).set("recordQuantize", m.settings.recordQuantize)
                              .set("countIn", m.settings.countIn).set("keys", keys));
        Json midi = Json::array();
        for (const auto &d : m.midiDevices) midi.push(Json::object().set("id", d.id).set("name", d.name).set("listening", d.listening));
        j.set("armed", m.armed).set("midiDevices", midi);
        if (!stable) j.set("midiMessages", (int)m.midiMessages);
        Json devs = Json::array();
        for (const auto &d : m.devices)
            devs.push(Json::object().set("id", d.id).set("name", d.name).set("dir", d.dir).set("channels", d.channels).set("rate", d.rate));
        j.set("devices", devs);
        Json tr = Json::object();
        tr.set("playing", m.transport.playing);
        if (!stable) tr.set("position", m.transport.position).set("tempo", m.transport.tempo).set("tempoId", m.transport.tempoId);
        tr.set("loopFrom", m.transport.loopFrom).set("loopTo", m.transport.loopTo).set("loopClip", m.transport.loopClip).set("cue", m.transport.cue)
            .set("punchFrom", m.transport.punchFrom).set("punchTo", m.transport.punchTo);
        if (!stable) tr.set("latencyMs", m.transport.latencyMs);
        tr.set("device", m.transport.device);
        if (!stable)
            tr.set("masterPeak", Json::array().push(Json::number(m.transport.masterPeak[0])).push(Json::number(m.transport.masterPeak[1])))
                .set("masterRms", Json::array().push(Json::number(m.transport.masterRms[0])).push(Json::number(m.transport.masterRms[1])))
                .set("masterHold", Json::array().push(Json::number(m.transport.masterHold[0])).push(Json::number(m.transport.masterHold[1])))
                .set("masterClip", m.transport.masterClip).set("driver", m.transport.driver).set("xruns", (int)m.transport.xruns)
                .set("countingIn", m.transport.countingIn).set("recorded", m.transport.recorded);
        tr.set("recording", m.transport.recording);
        j.set("transport", tr);
        if (!stable)
        {
            Json bands = Json::array();
            for (double b : m.loudness.spectrum) bands.push(Json::number(b));
            j.set("loudness", Json::object().set("momentary", m.loudness.momentary).set("shortTerm", m.loudness.shortTerm)
                                  .set("integrated", m.loudness.integrated).set("truePeak", m.loudness.truePeak)
                                  .set("correlation", m.loudness.correlation).set("spectrum", bands));
        }
        Json au = Json::object();
        au.set("file", m.audition.file).set("playing", m.audition.playing);
        if (!stable) au.set("progress", m.audition.progress);
        j.set("audition", au);
        Json types = Json::array();
        for (const auto &d : m.deviceTypes)
        {
            Json names = Json::array();
            for (const auto &n : d.noteNames) names.push(Json::object().set("note", n.note).set("name", n.name));
            types.push(Json::object().set("name", d.name).set("label", d.label).set("kind", d.kind).set("noteNames", names).set("takesSample", d.takesSample));
        }
        j.set("deviceTypes", types);
        auto vocab = [](const std::vector<VocabModel> &v) { // R-ROLL-11
            Json a = Json::array();
            for (const auto &x : v) a.push(Json::object().set("name", x.name).set("label", x.label));
            return a;
        };
        j.set("chords", vocab(m.chords)).set("scales", vocab(m.scales)).set("grooves", vocab(m.grooves));
        Json entries = Json::array();
        for (const auto &e : m.browser.entries) entries.push(Json::object().set("name", e.name).set("path", e.path).set("kind", e.kind));
        j.set("browser", Json::object().set("path", m.browser.path).set("entries", entries).set("search", m.browser.search));
        Json library = Json::array(); // R-PRESET-3
        for (const auto &x : m.library) library.push(Json::object().set("path", x.path).set("tags", strings(x.tags)).set("favourite", x.favourite));
        j.set("library", library);
        Json plugins = Json::array(); // R-VST-6
        for (const auto &x : m.plugins)
            plugins.push(Json::object().set("id", x.id).set("name", x.name).set("vendor", x.vendor).set("kind", x.kind).set("path", x.path));
        j.set("plugins", plugins);
        if (!stable) j.set("revision", m.revision);
        return j;
    }
}
}
