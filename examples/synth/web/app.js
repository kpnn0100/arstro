/*
 *  Basic Synth example — web driver. Wires the wasm module (Arstro DSP + Artboard)
 *  to a Canvas2D context (the adapter draws via window.__abctx) and Web Audio.
 *
 *  The UI renders immediately on load (rAF loop); clicking "Start audio" only adds
 *  sound — drawing/animation never waits on the audio gesture.
 */
"use strict";

let M = null, audio = null, audioNode = null, started = false, t0 = null;
const W = 1072, H = 480, BUF = 1024, CH = 2; // 2x the 536x240 design
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");

const setStatus = (t) => (document.getElementById("status").textContent = t);

function loop() {
  if (M) {
    if (t0 === null) t0 = performance.now();
    M.frame(performance.now() - t0);
  }
  requestAnimationFrame(loop);
}

createSynthModule().then((mod) => {
  M = mod;
  M.init(W, H, 48000);            // build the scene now so it animates in immediately
  setStatus("ready — click ▶ Start audio for sound, then play A–K");
  requestAnimationFrame(loop);
}).catch((e) => setStatus("wasm load failed: " + e));

document.getElementById("start").addEventListener("click", () => {
  if (started || !M) return;
  audio = new (window.AudioContext || window.webkitAudioContext)();
  M.init(W, H, audio.sampleRate); // re-init at the real device sample rate
  // Persist the node: a local would be GC'd and go silent (ScriptProcessorNode pitfall).
  audioNode = audio.createScriptProcessor(BUF, 0, CH);
  audioNode.onaudioprocess = (e) => {
    const base = M.renderAudio(BUF) >> 2, heap = M.HEAPF32;
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < BUF; i++) { L[i] = heap[base + i * 2]; R[i] = heap[base + i * 2 + 1]; }
  };
  audioNode.connect(audio.destination);
  audio.resume();
  window.__arstroAudio = { audio, audioNode };
  started = true;
  setStatus("playing — " + audio.sampleRate + " Hz · play A–K");
});

// Mouse -> raw pointer events; the wasm recognizer derives click/drag/double-click.
function xy(e) { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
canvas.addEventListener("mousedown",  (e) => { if (M) { const [x, y] = xy(e); M.pointer(0, x, y, e.button, performance.now()); } });
canvas.addEventListener("mousemove",  (e) => { if (M) { const [x, y] = xy(e); M.pointer(1, x, y, e.button, performance.now()); } });
window.addEventListener("mouseup",    (e) => { if (M) { const [x, y] = xy(e); M.pointer(2, x, y, e.button, performance.now()); } });
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
canvas.addEventListener("dblclick",   (e) => e.preventDefault());

// Forward keys to the app: letters (a w s e d f t g y h u j k) play one octave,
// arrows navigate pages. SynthApp::key owns all key semantics.
const ARROWS = { ArrowLeft: 37, ArrowUp: 38, ArrowRight: 39, ArrowDown: 40 };
function keyCode(e) {
  if (ARROWS[e.key] != null) return ARROWS[e.key];
  const k = e.key.toLowerCase();
  return k.length === 1 ? k.charCodeAt(0) : 0;
}
addEventListener("keydown", (e) => {
  if (!M || e.repeat) return;
  const c = keyCode(e);
  if (c) { if (ARROWS[e.key] != null) e.preventDefault(); M.key(c, true); }
});
addEventListener("keyup", (e) => {
  if (!M) return;
  const c = keyCode(e);
  if (c) M.key(c, false);
});
