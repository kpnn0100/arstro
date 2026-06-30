/*
 *  Cosmo by arstro — web driver. Wires the wasm module to a Canvas2D context and an
 *  rAF loop; the canvas fills the window and the UI reflows on resize. Opened files
 *  are decoded in the browser (createImageBitmap -> canvas -> ImageData) and pushed
 *  into the wasm heap via loadImage.
 */
"use strict";

let M = null, t0 = null;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");
const setStatus = (s) => (document.getElementById("status").textContent = s);

function viewSize() {
  return { w: Math.max(640, Math.floor(window.innerWidth - 4)),
           h: Math.max(360, Math.floor(window.innerHeight - 52)) };
}
function applySize() {
  const s = viewSize();
  canvas.width = s.w; canvas.height = s.h;
  if (M) M.resize(s.w, s.h);
}
window.addEventListener("resize", applySize);

function loop() {
  if (M) { if (t0 === null) t0 = performance.now(); M.frame(performance.now() - t0); }
  requestAnimationFrame(loop);
}

createCosmoModule().then((mod) => {
  M = mod;
  const s = viewSize();
  canvas.width = s.w; canvas.height = s.h;
  M.init(s.w, s.h);
  setStatus("ready - open an image");
  requestAnimationFrame(loop);
}).catch((e) => setStatus("wasm load failed: " + e));

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
canvas.addEventListener("mousedown", (e) => { if (M) { const [x, y] = xy(e); M.pointer(0, x, y, e.button, performance.now(), e.altKey, e.shiftKey, e.ctrlKey || e.metaKey); } });
canvas.addEventListener("mousemove", (e) => { if (M) { const [x, y] = xy(e); M.pointer(1, x, y, e.button, performance.now(), e.altKey, e.shiftKey, e.ctrlKey || e.metaKey); } });
window.addEventListener("mouseup", (e) => { if (M) { const [x, y] = xy(e); M.pointer(2, x, y, e.button, performance.now(), e.altKey, e.shiftKey, e.ctrlKey || e.metaKey); } });
canvas.addEventListener("contextmenu", (e) => e.preventDefault());
