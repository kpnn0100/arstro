# What the synth and the genre were learned from

## The analog synth

| source | what it gave |
|---|---|
| Adam Szabo, *How to Emulate the Super Saw* (KTH, 2010), its numbers as published in code at https://gist.github.com/audionerd/fe50790b7601cba65ddd855caffb05ad | the JP-8000's seven voices (offsets ±0.0195, ±0.0629, ±0.110 of the fundamental), the eleventh-order detune curve, the centre/side mix law, the high-pass at the fundamental, free-running random phases |
| Vadim Zavalishin, *The Art of VA Filter Design* 2.1.0 (https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf) | the trapezoidal one-pole, the ladder with its feedback solved inside the sample (zero-delay feedback), the multimode outputs |
| KVR and synth-diy discussions of band-limited steps (https://www.kvraudio.com/forum/viewtopic.php?p=5269992, https://synth-diy.org/pipermail/synth-diy/2021-February/167019.html) | polynomial band-limited steps are the cheap, good anti-aliasing for saw and pulse, and 2× oversampling on top cleans what is left. Measured here: a two-point step left aliases 57 dB down; the four-point one 86 dB |
| Xfer Serum and NI Massive, as their users describe them (https://www.productionmusiclive.com/blogs/news/10-supersaw-tips-for-xfer-serum-get-a-big-lush-sound-without-layering, https://www.productionmusiclive.com/blogs/news/how-to-make-a-future-bass-synth-in-serum-beginners) | what a modern synth offers a sound designer: unison with detune and blend per oscillator, one oscillator an octave up, a noise source, a filter that can be off, envelopes dragged onto pitch and cutoff, an LFO at a note value. Serum's and Massive's wavetables, FX racks and modulation matrices are NOT copied: this is an analog synth |

## Future bass

| source | what it gave |
|---|---|
| Native Instruments, "How to make future bass" (https://blog.native-instruments.com/how-to-make-future-bass/) | 130–160 bpm half time; 7th and 9th chords; the lead as layers (a pluck, a sustained saw with vibrato) |
| EDMProd, "How to make future bass like Flume" (https://www.edmprod.com/how-to-make-future-bass-like-flume/) | kick on 1, snare on 3; snare and clap layered in one room; a mid bass over a sub, high-passed; the build in two halves ending in silence and one clap |
| Loopmasters, "How to make a future bass drop" (https://www.loopmasters.com/articles/4372-How-To-Make-A-Future-Bass-Drop-A-Future-Bass-Production-Tutorial) | two detuned saw stacks an octave apart with a quick pitch envelope up at each note; chords and bass ducked by kick AND snare; fills after the snare; riser, cut, impact |
| musicproductionwiki, "How to recreate future bass chords" (https://musicproductionwiki.com/articles/how-to-recreate-future-bass-chords — its numbers marked unverified there) | a supersaw of 7–12 voices at moderate detune; chords high-passed at 150–200 Hz; a cut around 150 Hz; the pump timed to the beat |

**Honest limits.** No interview or session breakdown from Illenium, San Holo or Martin Garrix was found; their style
is described here as habits of the genre (a quiet verse on piano or guitar, a drop whose lead is its chords, long
soaring notes or short syncopated stabs), and no melody, progression or sound of theirs is reproduced. The Szabo
thesis itself was not fetched; its numbers are taken from code that cites it.
