/*
 *  Cosmo by arstro — web driver. Wires the wasm module to a Canvas2D context and
 *  an rAF loop; decodes opened files in the browser (createImageBitmap -> canvas
 *  -> ImageData) and pushes straight RGBA8 bytes into the wasm heap via loadImage.
 */
"use strict";

let M = null, t0 = null;
const W = 1280, H = 860;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");
const setStatus = (s) => (document.getElementById("status").textContent = s);

function loop() {
  if (M) { if (t0 === null) t0 = performance.now(); M.frame(performance.now() - t0); }
  requestAnimationFrame(loop);
}

createCosmoModule().then((mod) => {
  M = mod;
  M.init(W, H);
  setStatus("ready — open an image");
  requestAnimationFrame(loop);
}).catch((e) => setStatus("wasm load failed: " + e));

// Decode a File to straight RGBA8 and hand it to the wasm engine.
async function loadFile(file) {
  const bmp = await createImageBitmap(file);
  const off = document.createElement("canvas");
  off.width = bmp.width; off.height = bmp.height;
  const octx = off.getContext("2d");
  octx.drawImage(bmp, 0, 0);
  const data = octx.getImageData(0, 0, bmp.width, bmp.height).data; // straight RGBA8, top-down
  const ptr = M.allocInput(data.length);   // C++ owns the staging buffer
  M.HEAPU8.set(data, ptr);                  // re-read HEAPU8 each time (memory may have grown)
  M.loadImage(bmp.width, bmp.height, file.name);
}

document.getElementById("file").addEventListener("change", async (e) => {
  if (!M) return;
  for (const file of e.target.files) {
    try { await loadFile(file); } catch (err) { setStatus("decode failed: " + err); }
  }
  setStatus("");
  e.target.value = "";
});

function xy(e) { const r = canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
canvas.addEventListener("mousedown", (e) => { if (M) { const [x, y] = xy(e); M.pointer(0, x, y, e.button, performance.now()); } });
canvas.addEventListener("mousemove", (e) => { if (M) { const [x, y] = xy(e); M.pointer(1, x, y, e.button, performance.now()); } });
window.addEventListener("mouseup", (e) => { if (M) { const [x, y] = xy(e); M.pointer(2, x, y, e.button, performance.now()); } });
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
