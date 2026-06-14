"use strict";

let M = null;
let t0 = null;
const W = 960;
const H = 560;
const canvas = document.getElementById("c");
window.__abctx = canvas.getContext("2d");

const setStatus = (text) => {
  document.getElementById("status").textContent = text;
};

function loop(now) {
  if (M) {
    if (t0 === null) t0 = now;
    M.frame(now - t0);
  }
  requestAnimationFrame(loop);
}

createUiDemoModule().then((mod) => {
  M = mod;
  M.init(W, H);
  setStatus("ready — click controls and type anywhere");
  requestAnimationFrame(loop);
}).catch((error) => setStatus("wasm load failed: " + error));

function localXY(event) {
  const rect = canvas.getBoundingClientRect();
  return [event.clientX - rect.left, event.clientY - rect.top];
}

canvas.addEventListener("mousedown", (event) => {
  if (!M) return;
  const [x, y] = localXY(event);
  M.pointer(0, x, y, event.button, performance.now());
});

canvas.addEventListener("mousemove", (event) => {
  if (!M) return;
  const [x, y] = localXY(event);
  M.pointer(1, x, y, event.button, performance.now());
});

window.addEventListener("mouseup", (event) => {
  if (!M) return;
  const [x, y] = localXY(event);
  M.pointer(2, x, y, event.button, performance.now());
});

canvas.addEventListener("contextmenu", (event) => event.preventDefault());

window.addEventListener("keydown", (event) => {
  if (!M) return;
  M.keyDown(event.keyCode || event.which, event.shiftKey, event.ctrlKey, event.altKey);
  if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
    M.textInput(event.key);
  }
});

window.addEventListener("keyup", (event) => {
  if (!M) return;
  M.keyUp(event.keyCode || event.which, event.shiftKey, event.ctrlKey, event.altKey);
});