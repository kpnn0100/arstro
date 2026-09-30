// cosmo — mvvm_test.mjs: the web front end is MVVM, checked in a real browser (R-NTWB-7/8, R-NTWB-5).
//
// The claim this proves is the one the user asked for: "each client view is independent, so that we
// can have multi client, each client can have their own UI suit the screen but one edit will affect
// each other if using the same app session". Two pages of ONE Arstro Remote session - a desktop
// window and a phone - must show different shells, see each other's edits through the model, keep
// their own view state, and count each other (presence); a page in ANOTHER session must not see them.
//
// It drives the page's own view-model (`window.cosmo.vm`, the same intents the widgets call) and
// reads what the page renders, over the Chrome DevTools protocol - no npm packages (Node >= 22 has
// WebSocket and fetch built in). Run it from any machine that reaches the board:
//
//   ARSTRO_URL=http://<board>:8080 ARSTRO_TOKEN=<web password> PHOTOS=/abs/a.jpg,/abs/b.jpg \
//     node apps/cosmo/tests/web/mvvm_test.mjs [--chrome /usr/bin/google-chrome] [--shots DIR]
//
// PHOTOS are paths ON THE BOARD. It starts two sessions of cosmo (`?session=new`) and stops them at
// the end; the board needs Arstro Remote >= NTWB 1.1 with cosmo installed (`cosmo-cc ntwb install`).
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const args = process.argv.slice(2);
const opt = (name, def) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : def; };
const BASE = (process.env.ARSTRO_URL || "").replace(/\/$/, "");
const TOKEN = process.env.ARSTRO_TOKEN || "";
const PHOTOS = (process.env.PHOTOS || "").split(",").filter(Boolean);
const CHROME = opt("--chrome", process.env.CHROME || "google-chrome");
const SHOTS = opt("--shots", null);
if (!BASE || !PHOTOS.length) {
  console.error("usage: ARSTRO_URL=... ARSTRO_TOKEN=... PHOTOS=/a.jpg,/b.jpg node mvvm_test.mjs");
  process.exit(2);
}

let failures = 0;
function check(ok, what) {
  console.log((ok ? "PASS  " : "FAIL  ") + what);
  if (!ok) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(fn, ms = 15000, step = 150) {
  const end = Date.now() + ms;
  for (;;) {
    let v;
    try { v = await fn(); } catch { v = undefined; }
    if (v) return v;
    if (Date.now() > end) return v;
    await sleep(step);
  }
}

// ------------------------------------------------------------------ a minimal CDP client
async function launchChrome() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "cosmo-mvvm-"));
  const port = 9300 + Math.floor(Math.random() * 500);
  const proc = spawn(CHROME, ["--headless=new", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
    `--remote-debugging-port=${port}`, `--user-data-dir=${dir}`, "about:blank"], { stdio: "ignore" });
  const version = await until(async () => (await fetch(`http://127.0.0.1:${port}/json/version`)).json(), 15000);
  if (!version) throw new Error("Chrome did not start: " + CHROME);
  const browser = new Page(version.webSocketDebuggerUrl);
  await browser.ready;
  return { port, proc, dir, browser };
}

class Page {
  static async open(chrome, { width, height, mobile }) {
    // Its own WINDOW, not a tab: a background tab is hidden and never runs requestAnimationFrame,
    // so its eased fades would never finish - two pages must render side by side like two screens.
    const { targetId } = await chrome.browser.send("Target.createTarget", { url: "about:blank", newWindow: true });
    const list = await (await fetch(`http://127.0.0.1:${chrome.port}/json/list`)).json();
    const t = list.find((x) => x.id === targetId);
    const p = new Page(t.webSocketDebuggerUrl);
    await p.ready;
    await p.send("Page.enable");
    await p.send("Runtime.enable");
    await p.send("Emulation.setDeviceMetricsOverride", { width, height, deviceScaleFactor: 1, mobile: !!mobile });
    if (mobile) await p.send("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
    return p;
  }
  constructor(url) {
    this.ws = new WebSocket(url);
    this.n = 0;
    this.pending = new Map();
    this.errors = [];
    this.ready = new Promise((r) => { this.ws.onopen = r; });
    this.ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.id && this.pending.has(m.id)) { const { res, rej } = this.pending.get(m.id); this.pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
      if (m.method === "Runtime.exceptionThrown") this.errors.push(m.params.exceptionDetails.text + " " + (m.params.exceptionDetails.exception?.description || ""));
      if (m.method === "Runtime.consoleAPICalled" && m.params.type === "error") this.errors.push(m.params.args.map((a) => a.value ?? a.description).join(" "));
    };
  }
  send(method, params = {}) {
    const id = ++this.n;
    this.ws.send(JSON.stringify({ id, method, params }));
    return new Promise((res, rej) => this.pending.set(id, { res, rej }));
  }
  async eval(expr) {
    const r = await this.send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
  }
  async goto(url) { await this.send("Page.navigate", { url }); await sleep(300); await until(() => this.eval("document.readyState === 'complete'")); }
  async shot(file) {
    if (!SHOTS) return;
    const r = await this.send("Page.captureScreenshot", { format: "png" });
    fs.mkdirSync(SHOTS, { recursive: true });
    fs.writeFileSync(path.join(SHOTS, file), Buffer.from(r.data, "base64"));
  }
  /** Close the PAGE (its tab and its WebSocket to the app), not just this DevTools connection. */
  close() {
    this.send("Page.close").catch(() => {});
    setTimeout(() => this.ws.close(), 300);
  }
}

// ------------------------------------------------------------------ the host (Arstro Remote)
async function op(name, body = {}) {
  const r = await fetch(`${BASE}/api/op/${name}`, { method: "POST", headers: { "Content-Type": "application/json", Authorization: "Bearer " + TOKEN }, body: JSON.stringify(body) });
  const j = await r.json();
  if (j.ok === false) throw new Error(`${name}: ${j.error}`);
  return j.data;
}
const command = (session, line) => op("apps.call", { app: "cosmo", session, method: "command", params: { line }, timeout: 120 });

const vmState = `(() => { const vm = window.cosmo && window.cosmo.vm; if (!vm || !vm.ready.value) return null;
  return { screen: vm.screen.value, layout: document.querySelector("#app > .shell:not(.away)")?.dataset.layout,
           session: vm.presence.value.session, clients: vm.presence.value.clients,
           exposure: vm.params.value.exposure, tab: vm.view.tab.value, compare: vm.view.compare.value,
           nodes: vm.nodes.value.length, frame: !!vm.frame.value,
           overflowX: document.documentElement.scrollWidth > window.innerWidth + 1 }; })()`;

async function main() {
  const chrome = await launchChrome();
  const stop = [];
  try {
    // A fresh session, with a project of its own (a scratch .cmp beside the first photo's folder).
    const s1 = (await op("apps.launch", { app: "cosmo", session: "new" })).session;
    stop.push(s1);
    const dir = path.posix.dirname(PHOTOS[0]);
    await command(s1, `project new ${dir}/.mvvm-test-${s1}.cmp`);
    await command(s1, `import ${PHOTOS.map((p) => (/\s/.test(p) ? `"${p}"` : p)).join(" ")}`);

    const desk = await Page.open(chrome, { width: 1600, height: 1000 });
    const phone = await Page.open(chrome, { width: 393, height: 852, mobile: true });
    for (const p of [desk, phone]) {
      await p.goto(`${BASE}/api/ping`);
      await p.eval(`fetch("/api/login", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({token: ${JSON.stringify(TOKEN)}})}).then(r => r.ok)`);
      await p.goto(`${BASE}/apps/cosmo/?session=${s1}`);
    }
    // Ready = the shell for this page's screen is on screen (its modules load after the model may
    // already have arrived) and the model is the editor's.
    const d0 = await until(async () => { const v = await desk.eval(vmState); return v && v.layout && v.screen === "editor" && v.frame && v.clients === 2 ? v : null; }, 60000);
    const p0 = await until(() => phone.eval(vmState).then((v) => (v && v.layout && v.screen === "editor" ? v : null)), 30000);
    check(d0 && d0.layout === "desktop", `the desktop page shows the desktop shell (${d0 && d0.layout})`);
    check(p0 && p0.layout === "touch", `the phone page shows the touch shell (${p0 && p0.layout})`);
    check(d0 && p0 && d0.session === s1 && p0.session === s1, "both pages are in the session they asked for");
    check(d0 && d0.clients === 2 && p0 && p0.clients === 2, "presence: each page knows two share the session");
    check(d0 && !d0.overflowX && p0 && !p0.overflowX, "neither page scrolls sideways");
    await desk.shot("desktop-editor.png");
    await phone.shot("phone-editor.png");

    // One edit, through the desktop page's view-model -> the model -> the phone page.
    await desk.eval(`window.cosmo.vm.setFields({exposure: "1.25"})`);
    const seen = await until(() => phone.eval(vmState).then((v) => (v && Math.abs(v.exposure - 1.25) < 1e-3 ? v : null)), 5000);
    check(!!seen, "an edit made on the desktop page reaches the phone page through the model");
    await phone.eval(`window.cosmo.vm.setFields({exposure: "-0.5"})`);
    const back = await until(() => desk.eval(vmState).then((v) => (v && Math.abs(v.exposure + 0.5) < 1e-3 ? v : null)), 5000);
    check(!!back, "and one made on the phone reaches the desktop");

    // View state is each page's own.
    await desk.eval(`(window.cosmo.vm.view.tab.value = "mask", window.cosmo.vm.view.compare.value = "split", true)`);
    await sleep(600);
    const pv = await phone.eval(vmState);
    check(pv && pv.tab !== "mask" && pv.compare === "after", `the desktop's tab and compare mode stay on the desktop (phone: ${pv && pv.tab}, ${pv && pv.compare})`);

    // Leaving the project is done to the SESSION (the user, 2026-09-30: "i jump back to home page,
    // and close session, when i open again, it should be in home page of cosmo but it jump
    // directly to project edit screen"): after a page goes Home and is closed, a page opened on the
    // session starts on Home, and the session's other pages are on Home too.
    await desk.eval(`window.cosmo.vm.goHome()`);
    const phoneHome = await until(() => phone.eval(vmState).then((v) => (v && v.screen === "home" ? v : null)), 5000);
    check(!!phoneHome, "going Home on the desktop takes the session - the phone page - Home too");
    desk.close();
    await until(() => phone.eval(vmState).then((v) => (v && v.clients === 1 ? v : null)), 5000);   // it has left
    const again = await Page.open(chrome, { width: 1600, height: 1000 });
    await again.goto(`${BASE}/api/ping`);
    await again.eval(`fetch("/api/login", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({token: ${JSON.stringify(TOKEN)}})}).then(r => r.ok)`);
    await again.goto(`${BASE}/apps/cosmo/?session=${s1}`);
    const reopened = await until(() => again.eval(vmState).then((v) => (v && v.layout && v.screen !== "connecting" ? v : null)), 30000);
    check(reopened && reopened.screen === "home", `a page opened after going Home starts on Home (${reopened && reopened.screen})`);
    // ...and opening the still-loaded project from Home is one command for the session, no reload.
    await again.eval(`window.cosmo.vm.backToEditor()`);
    const backEd = await until(() => phone.eval(vmState).then((v) => (v && v.screen === "editor" ? v : null)), 5000);
    check(!!backEd, "back to the editor from Home takes the session back too");
    const desk2 = again;

    // Another session is another model: the edit above is not there.
    const s2 = (await op("apps.launch", { app: "cosmo", session: "new" })).session;
    stop.push(s2);
    await command(s2, `project new ${dir}/.mvvm-test-${s2}.cmp`);
    await command(s2, `import ${PHOTOS[0]}`);
    const other = await Page.open(chrome, { width: 1280, height: 800 });
    await other.goto(`${BASE}/api/ping`);
    await other.eval(`fetch("/api/login", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify({token: ${JSON.stringify(TOKEN)}})}).then(r => r.ok)`);
    await other.goto(`${BASE}/apps/cosmo/?session=${s2}`);
    const o = await until(() => other.eval(vmState).then((v) => (v && v.screen === "editor" && v.nodes === 1 ? v : null)), 60000);
    check(o && o.session === s2 && o.clients === 1 && Math.abs(o.exposure) < 1e-3, "a page of another session has its own model and its own presence");
    other.close();
    const dAfter = await until(() => desk2.eval(vmState).then((v) => (v && v.clients === 2 ? v : null)), 3000);
    check(!!dAfter, "and it is not counted in the first session");

    const errs = [...desk.errors, ...desk2.errors, ...phone.errors];
    check(errs.length === 0, "no page errors" + (errs.length ? ": " + errs.slice(0, 3).join(" | ") : ""));
    desk2.close();
    phone.close();
  } finally {
    for (const s of stop) { try { await op("apps.stop", { app: "cosmo", session: s }); } catch { /* already gone */ } }
    const gone = new Promise((r) => chrome.proc.once("exit", r));
    chrome.proc.kill();
    await Promise.race([gone, sleep(3000)]);
    try { fs.rmSync(chrome.dir, { recursive: true, force: true }); } catch { /* Chrome's last writes; a temp dir */ }
  }
  console.log(failures ? `\n${failures} failed` : "\nall passed");
  process.exit(failures ? 1 : 0);
}

main().catch((e) => { console.error(e); process.exit(1); });
