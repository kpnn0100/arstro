/*
 *  Scope example — web driver. The UI renders immediately on load (rAF loop);
 *  clicking "Start audio" only adds sound.
 */
"use strict";

let M = null, audio = null, audioNode = null, started = false, t0 = null;
const W = 640, H = 360, BUF = 1024, CH = 2;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");

const setStatus = (t) => (document.getElementById("status").textContent = t);

function loop() {
  if (M) { if (t0 === null) t0 = performance.now(); M.frame(performance.now() - t0); }
  requestAnimationFrame(loop);
}

createScopeModule().then((mod) => {
  M = mod;
  M.init(W, H, 48000);
  setStatus("ready — click ▶ Start audio, then play A–K");
  requestAnimationFrame(loop);
}).catch((e) => setStatus("wasm load failed: " + e));

document.getElementById("start").addEventListener("click", () => {
  if (started || !M) return;
  audio = new (window.AudioContext || window.webkitAudioContext)();
  M.init(W, H, audio.sampleRate);
  audioNode = audio.createScriptProcessor(BUF, 0, CH); // persistent: a local would be GC'd -> silence
  audioNode.onaudioprocess = (e) => {
    const base = M.renderAudio(BUF) >> 2, heap = M.HEAPF32;
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < BUF; i++) { L[i] = heap[base + i * 2]; R[i] = heap[base + i * 2 + 1]; }
  };
  audioNode.connect(audio.destination);
  audio.resume();
  window.__arstroAudio = { audio, audioNode };
  started = true;
  setStatus("playing — " + audio.sampleRate + " Hz");
});

const KEYS = "awsedftgyhujk";
const held = {};
addEventListener("keydown", (e) => {
  if (!M || e.repeat) return;
  const i = KEYS.indexOf(e.key.toLowerCase());
  if (i < 0 || held[e.key]) return;
  held[e.key] = 60 + i; M.noteOn(60 + i, 0.85);
});
addEventListener("keyup", (e) => {
  if (!M) return;
  const n = held[e.key];
  if (n != null) { M.noteOff(n); delete held[e.key]; }
});
