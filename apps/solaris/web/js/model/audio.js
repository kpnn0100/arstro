/*
 * Solaris — model/audio.js: hearing the engine IN THIS PAGE (R-SVC-7: `settings set driver=web`).
 *
 * With the driver "web", `solaris-cc ntwb serve` plays into the pages that listen instead of the machine's
 * speakers (cli/WebAudio.h): `audio.listen {on}` makes this page one of them, and the engine's sound
 * arrives as blobs on the stream `audio` — little-endian 16-bit interleaved PCM, >= 20 ms each, meta
 * {seq, rate, channels, frames, gen}. This class schedules them with the Web Audio API:
 *
 *   - one AudioContext, made (or resumed) inside the user's click — a browser plays nothing before one;
 *   - each blob becomes an AudioBuffer started at `next`, the end of the one before, so the sound is
 *     gapless; the first one (and the first after an underrun) waits kTargetS (the jitter buffer — the
 *     machine reports the same 120 ms as the device's latency, WebAudioOut::kPageLatencyS, so the
 *     playhead shows what is heard);
 *   - a newer `gen` (a stop, a seek, another song) drops everything scheduled from the older one;
 *   - more than kMaxAheadS queued (the two clocks drifted) drops a blob instead of growing the delay.
 * AudioBufferSourceNode, not an AudioWorklet: a worklet needs a secure context, and a page of Arstro
 * Remote on the LAN is plain http.
 *
 *   const a = new PageAudio(session)
 *   a.start() -> Promise   call it FROM a click; listens and resumes the context
 *   a.stop()  -> Promise   stops listening, drops what is scheduled
 *   a.listening: Signal<boolean>     a.error: Signal<string>
 */
import { signal } from "../core/signal.js";

const kTargetS = 0.12;      // the jitter buffer (WebAudioOut::kPageLatencyS)
const kMaxAheadS = 0.6;     // more queued than this: the clocks drifted — drop, do not grow the delay

export class PageAudio {
  constructor(session) {
    this.session = session;
    this.listening = signal(false);
    this.error = signal("");
    this.ctx = null;
    this.gen = null;
    this.next = 0;
    this.sources = new Set();
    this._chain = Promise.resolve();
    session.bridge.onBlob("audio", (blob, header) => {
      // blobs arrive in order; their bytes are read asynchronously, so keep the order with one chain
      this._chain = this._chain.then(() => this._play(blob, header)).catch((e) => console.error(e));
    });
    // the session restarted (or this page reconnected): listen again if it was listening
    session.bridge.onReady(() => { if (this.listening.peek()) this._listen(true).catch(() => {}); });
  }

  /** Call from a click: the browser only lets a page start sound inside one. */
  start() {
    try {
      if (!this.ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (!AC) throw new Error("this browser cannot play sound (no Web Audio)");
        this.ctx = new AC({ latencyHint: "interactive" });
      }
      if (this.ctx.state === "suspended") this.ctx.resume();
    } catch (e) {
      this.error.value = String(e.message || e);
      return Promise.reject(e);
    }
    return this._listen(true);
  }

  stop() {
    this._drop();
    this.gen = null;
    return this._listen(false);
  }

  async _listen(on) {
    try {
      const r = await this.session.call("audio.listen", { on });
      this.listening.value = on;
      this.error.value = "";
      return r;
    } catch (e) {
      this.listening.value = false;
      this.error.value = String(e.message || e);
      throw e;
    }
  }

  _drop() {
    for (const s of this.sources) { try { s.stop(); } catch { /* already ended */ } }
    this.sources.clear();
    this.next = 0;
  }

  async _play(blob, header) {
    if (!this.listening.peek() || !this.ctx) return;
    const meta = (header && header.meta) || {};
    const rate = meta.rate || 48000, ch = Math.max(1, meta.channels || 2);
    const bytes = blob instanceof ArrayBuffer ? blob : await blob.arrayBuffer();
    const pcm = new Int16Array(bytes);
    const frames = Math.floor(pcm.length / ch);
    if (!frames) return;
    if (meta.gen !== this.gen) { this._drop(); this.gen = meta.gen; }   // a stop, a seek: the old sound goes
    const ctx = this.ctx, now = ctx.currentTime;
    if (this.next < now + 0.01) this.next = now + kTargetS;              // the start, or an underrun: rebuffer
    else if (this.next > now + kMaxAheadS) return;                       // drifted ahead: drop this one
    const buf = ctx.createBuffer(ch, frames, rate);
    for (let c = 0; c < ch; c++) {
      const out = buf.getChannelData(c);
      for (let i = 0, j = c; i < frames; i++, j += ch) out[i] = pcm[j] / 32768;
    }
    const src = ctx.createBufferSource();
    src.buffer = buf;
    src.connect(ctx.destination);
    src.start(this.next);
    this.next += frames / rate;
    this.sources.add(src);
    src.onended = () => this.sources.delete(src);
  }
}
