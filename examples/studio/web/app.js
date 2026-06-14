/*
 *  Studio example — web driver. Wires the wasm module (Arstro DSP + Artboard) to
 *  a Canvas2D context (the adapter draws via window.__abctx) and Web Audio.
 *
 *  The UI renders immediately on load (rAF loop); clicking "Start audio" only
 *  adds sound — drawing never waits on the audio gesture.
 */
"use strict";

let M = null, audio = null, audioNode = null, started = false, t0 = null;
const W = 900, H = 520, BUF = 1024, CH = 2;
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

createStudioModule().then((mod) => {
  M = mod;
  M.init(W, H, 48000);                 // build the scene now so it's visible immediately
  setStatus("ready — click ▶ Start audio for sound, then play A–K");
  requestAnimationFrame(loop);         // draw + animate without waiting for audio
}).catch((e) => setStatus("wasm load failed: " + e));

document.getElementById("start").addEventListener("click", () => {
  if (started || !M) return;
  audio = new (window.AudioContext || window.webkitAudioContext)();
  M.init(W, H, audio.sampleRate);      // re-init at the real device sample rate
  // Keep the node referenced on a persistent var: a local would be GC'd and go
  // silent (the classic ScriptProcessorNode pitfall).
  audioNode = audio.createScriptProcessor(BUF, 0, CH);
  audioNode.onaudioprocess = (e) => {
    const base = M.renderAudio(BUF) >> 2, heap = M.HEAPF32;
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < BUF; i++) { L[i] = heap[base + i * 2]; R[i] = heap[base + i * 2 + 1]; }
  };
  audioNode.connect(audio.destination);
  audio.resume();                      // contexts start suspended until a gesture
  window.__arstroAudio = { audio, audioNode }; // extra keepalive
  started = true;
  setStatus("playing — " + audio.sampleRate + " Hz · play A–K");
});

// Mouse -> raw pointer events (the adapter's input side). The wasm recognizer
// turns these into click/drag/double-click/right-click; works before audio too.
function xy(e) { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
canvas.addEventListener("mousedown",  (e) => { if (M) { const [x, y] = xy(e); M.pointer(0, x, y, e.button, performance.now()); } });
canvas.addEventListener("mousemove",  (e) => { if (M) { const [x, y] = xy(e); M.pointer(1, x, y, e.button, performance.now()); } });
window.addEventListener("mouseup",    (e) => { if (M) { const [x, y] = xy(e); M.pointer(2, x, y, e.button, performance.now()); } });
canvas.addEventListener("contextmenu", (e) => e.preventDefault()); // allow right-click gestures
canvas.addEventListener("dblclick",   (e) => e.preventDefault());

const KEYS = "awsedftgyhujk";
const held = {};
addEventListener("keydown", (e) => {
  if (!M || e.repeat) return;
  const i = KEYS.indexOf(e.key.toLowerCase());
  if (i < 0 || held[e.key]) return;
  held[e.key] = 60 + i; M.noteOn(60 + i, 0.85);   // glow works even before audio starts
});
addEventListener("keyup", (e) => {
  if (!M) return;
  const n = held[e.key];
  if (n != null) { M.noteOff(n); delete held[e.key]; }
});
