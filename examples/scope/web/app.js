/*
 *  Scope example — web driver. Wires the wasm module (Arstro DSP + Artboard) to a
 *  Canvas2D context and Web Audio. The Canvas2DTarget adapter draws via window.__abctx.
 */
"use strict";

let M = null, started = false;
const W = 640, H = 360, BUF = 1024, CH = 2;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");   // the adapter draws into this context

function setStatus(t) { document.getElementById("status").textContent = t; }

createScopeModule().then((mod) => {
  M = mod;
  setStatus("ready — click ▶ Start audio");
}).catch((e) => setStatus("wasm load failed: " + e));

document.getElementById("start").addEventListener("click", () => {
  if (started || !M) return;
  const audio = new (window.AudioContext || window.webkitAudioContext)();
  M.init(W, H, audio.sampleRate);

  const node = audio.createScriptProcessor(BUF, 0, CH);
  node.onaudioprocess = (e) => {
    const ptr = M.renderAudio(BUF);
    const base = ptr >> 2, heap = M.HEAPF32;
    const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
    for (let i = 0; i < BUF; i++) { L[i] = heap[base + i * 2]; R[i] = heap[base + i * 2 + 1]; }
  };
  node.connect(audio.destination);

  started = true;
  setStatus("playing — " + audio.sampleRate + " Hz");
  const t0 = performance.now();
  function loop() { M.frame(performance.now() - t0); requestAnimationFrame(loop); }
  requestAnimationFrame(loop);
});

// Keyboard: A–K chromatic octave from C4.
const KEYS = "awsedftgyhujk";
const held = {};
addEventListener("keydown", (e) => {
  if (!M || !started || e.repeat) return;
  const i = KEYS.indexOf(e.key.toLowerCase());
  if (i < 0 || held[e.key]) return;
  held[e.key] = 60 + i; M.noteOn(60 + i, 0.85);
});
addEventListener("keyup", (e) => {
  if (!M) return;
  const n = held[e.key];
  if (n != null) { M.noteOff(n); delete held[e.key]; }
});
