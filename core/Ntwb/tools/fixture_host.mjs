// fixture_host.mjs - serve an app's REAL web face over a FAKE NTWB, fed with the EXACT model a native
// reference shot was drawn from, so the web page can be compared 1:1 with the native PNG.
// Dependency-free Node (v22).
//
//   node fixture_host.mjs --app <id> --web <installed web dir> --fixtures <dir> [--port <p>] [--host 127.0.0.1]
//
//   --web       the INSTALLED web face (Cosmo's borrowed core/ui files are only there):
//                 `<app>-cc ntwb install --data-dir D [--web-dir W]` -> D/<id>/web
//   --fixtures  the shots' --model-json output, e.g. /tmp/fixtures/<id> (see below)
//   --port      0 or absent = any free port; the URL is printed: "fixture_host listening on http://…"
//
// Routes:
//   /apps/<id>/…                   the web dir (`/apps/<id>/` = index.html), no-store
//   /ntwb/ntwb.js                  the FAKE SDK, tools/ntwb_fake.js (its header says what it does / does not)
//   /fixtures/<file>               the fixtures dir as is (<name>.model.json, .view.json, .transport.json,
//                                  .peaks.json, .frame.png, the native PNG, _api.json)
//   /fixtures/<name>.bundle.json   {name, model, view, transport, peaks, api, manifest} composed from those files and
//                                  the installed manifest (<web>/../ntwb.json); 404 when the model is missing
//   /fixtures/index.json           [fixture names] (every <name>.model.json)
//   /                              an index page: a link per fixture to /apps/<id>/?fixture=<name>
//   /api/ping, POST /api/login     answered, so tools/shot.mjs works against this host too (any TOKEN)
//   anything else                  404
// Open a fixture: http://127.0.0.1:<port>/apps/<id>/?fixture=<name>   (add &fxstate=late to deliver the
// state as `state` messages after `ready` instead of inside it).
//
// The fixtures come from the native shot tools:
//   build/apps/interstellar/app/interstellar_app_shots --size 1440x900 --model-json --outdir <F>/interstellar
//   SOLARIS_UI_TEST_DIR=<scratch> build/apps/solaris/app/solaris_app_shots --size 1440x900 --model-json --outdir <F>/solaris
//   <app>-cc api --json > <F>/<app>/_api.json
//
// What this host does NOT do: run the app, a WebSocket, sessions, logins, the Files/board API, or
// anything a command line asks for - it serves files and a fake. Use a real Arstro Remote for the real thing.
//
// As a module: `import { startFixtureHost } from "./fixture_host.mjs"` ->
//   await startFixtureHost({app, web, fixtures, port, host}) -> {url, port, server, close()}
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE_SDK = path.join(HERE, "ntwb_fake.js");

const TYPES = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8", ".json": "application/json; charset=utf-8", ".svg": "image/svg+xml",
  ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".webp": "image/webp", ".gif": "image/gif",
  ".ico": "image/x-icon", ".woff2": "font/woff2", ".woff": "font/woff", ".ttf": "font/ttf", ".otf": "font/otf",
  ".txt": "text/plain; charset=utf-8", ".md": "text/plain; charset=utf-8", ".wav": "audio/wav", ".mp4": "video/mp4",
};

function send(res, status, body, type = "text/plain; charset=utf-8") {
  res.writeHead(status, { "Content-Type": type, "Cache-Control": "no-store" });
  res.end(body);
}

/** A file under `root`, or null when the path leaves it or is not a file. */
function inside(root, rel) {
  const p = path.resolve(root, "." + path.posix.normalize("/" + rel));
  if (p !== root && !p.startsWith(root + path.sep)) return null;
  try {
    const st = fs.statSync(p);
    if (st.isDirectory()) {
      const idx = path.join(p, "index.html");
      return fs.existsSync(idx) ? idx : null;
    }
    return st.isFile() ? p : null;
  } catch {
    return null;
  }
}

function sendFile(res, file) {
  res.writeHead(200, { "Content-Type": TYPES[path.extname(file).toLowerCase()] || "application/octet-stream", "Cache-Control": "no-store" });
  fs.createReadStream(file).pipe(res);
}

function readJson(file) {
  try { return JSON.parse(fs.readFileSync(file, "utf8")); } catch { return null; }
}

function fixtureNames(dir) {
  return fs.readdirSync(dir).filter((f) => f.endsWith(".model.json")).map((f) => f.slice(0, -".model.json".length)).sort();
}

export async function startFixtureHost({ app, web, fixtures, port = 0, host = "127.0.0.1", quiet = false }) {
  if (!app || !web || !fixtures) throw new Error("fixture_host: --app, --web and --fixtures are required");
  web = path.resolve(web);
  fixtures = path.resolve(fixtures);
  if (!fs.existsSync(path.join(web, "index.html"))) throw new Error(`fixture_host: ${web} has no index.html (an INSTALLED web dir?)`);
  if (!fs.existsSync(fixtures)) throw new Error(`fixture_host: no fixtures dir ${fixtures}`);
  const manifest = readJson(path.join(web, "..", "ntwb.json")) || { id: app, name: app };
  const prefix = `/apps/${app}`;

  const server = http.createServer((req, res) => {
    let u;
    try { u = new URL(req.url, "http://x"); } catch { return send(res, 400, "bad url"); }
    const p = decodeURIComponent(u.pathname);
    if (p === "/api/ping") return send(res, 200, JSON.stringify({ ok: true, fixtureHost: true }), TYPES[".json"]);
    if (p === "/api/login") return send(res, 200, JSON.stringify({ ok: true }), TYPES[".json"]);
    if (p === "/favicon.ico") { res.writeHead(204); return res.end(); }
    if (p === "/ntwb/ntwb.js") return sendFile(res, FAKE_SDK);
    if (p === prefix) { res.writeHead(302, { Location: prefix + "/" + u.search }); return res.end(); }
    if (p.startsWith(prefix + "/")) {
      const f = inside(web, p.slice(prefix.length + 1));
      return f ? sendFile(res, f) : send(res, 404, "not found: " + p);
    }
    if (p === "/fixtures/index.json") return send(res, 200, JSON.stringify(fixtureNames(fixtures)), TYPES[".json"]);
    const bundle = p.match(/^\/fixtures\/(.+)\.bundle\.json$/);
    if (bundle) {
      const name = bundle[1];
      if (name.includes("/") || name.includes("..")) return send(res, 400, "bad fixture name");
      const model = readJson(path.join(fixtures, name + ".model.json"));
      if (!model) return send(res, 404, `no fixture ${name} (no ${name}.model.json in ${fixtures})`);
      const out = { name, model, manifest: { id: manifest.id || app, name: manifest.name || app, version: manifest.version || "" } };
      for (const [k, f] of [["view", name + ".view.json"], ["transport", name + ".transport.json"], ["peaks", name + ".peaks.json"], ["api", "_api.json"]]) {
        const v = readJson(path.join(fixtures, f));
        if (v) out[k] = v;
      }
      return send(res, 200, JSON.stringify(out), TYPES[".json"]);
    }
    if (p.startsWith("/fixtures/")) {
      const f = inside(fixtures, p.slice("/fixtures/".length));
      return f ? sendFile(res, f) : send(res, 404, "not found: " + p);
    }
    if (p === "/") {
      const items = fixtureNames(fixtures)
        .map((n) => `<li><a href="${prefix}/?fixture=${encodeURIComponent(n)}">${n.replace(/[<&>]/g, "")}</a></li>`).join("");
      return send(res, 200, `<!doctype html><meta charset="utf-8"><title>fixtures: ${app}</title><h1>${app} fixtures</h1><ol>${items}</ol>`,
        TYPES[".html"]);
    }
    return send(res, 404, "not found: " + p);
  });
  await new Promise((resolve, reject) => { server.once("error", reject); server.listen(port, host, resolve); });
  const actual = server.address().port;
  const url = `http://${host}:${actual}`;
  if (!quiet) console.log(`fixture_host listening on ${url}  (${app}: ${fixtureNames(fixtures).length} fixtures, web ${web})`);
  return { url, port: actual, server, close: () => new Promise((r) => server.close(() => r())) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const args = process.argv.slice(2);
  const opt = (n, d) => { const i = args.indexOf(n); return i < 0 ? d : args[i + 1]; };
  if (args.includes("--help") || args.includes("-h")) {
    console.log("usage: node fixture_host.mjs --app <id> --web <installed web dir> --fixtures <dir> [--port <p>] [--host 127.0.0.1]");
    process.exit(0);
  }
  try {
    await startFixtureHost({ app: opt("--app"), web: opt("--web"), fixtures: opt("--fixtures"), port: +opt("--port", "0"), host: opt("--host", "127.0.0.1") });
  } catch (e) {
    console.error(String(e.message || e));
    process.exit(2);
  }
}
