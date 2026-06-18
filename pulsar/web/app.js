/*
 *  Pulsar by arstro — web driver. Wires the wasm module (Artboard) to a Canvas2D
 *  context and an rAF loop. UI-only (no audio yet).
 */
"use strict";

let M = null, t0 = null;
const W = 1080, H = 900;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");
const setStatus = (s) => (document.getElementById("status").textContent = s);

function loop() {
  if (M) { if (t0 === null) t0 = performance.now(); M.frame(performance.now() - t0); }
  requestAnimationFrame(loop);
}

createPulsarModule().then((mod) => {
  M = mod;
  M.init(W, H);
  setStatus("ready");
  requestAnimationFrame(loop);
}).catch((e) => setStatus("wasm load failed: " + e));

function xy(e) { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
canvas.addEventListener("mousedown", (e) => { if (M) { const [x, y] = xy(e); M.pointer(0, x, y, e.button, performance.now()); } });
canvas.addEventListener("mousemove", (e) => { if (M) { const [x, y] = xy(e); M.pointer(1, x, y, e.button, performance.now()); } });
window.addEventListener("mouseup", (e) => { if (M) { const [x, y] = xy(e); M.pointer(2, x, y, e.button, performance.now()); } });
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
