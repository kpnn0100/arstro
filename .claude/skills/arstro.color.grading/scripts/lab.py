#!/usr/bin/env python3
"""lab.py — the Grade Lab: one photo, many grades, the approver picks the best. That is how this skill
learns what GOOD grading is — not from a recipe, from the photographer's eye.

    python3 lab.py serve --project tokyo.before-grade.cmp [--plan plan.json] [--photos a.RAF,b.RAF]
                   [--port 8789] [--lab ~/photo/grade-lab]
    python3 lab.py summary            # what the verdicts say so far (data/judgments.jsonl)

Binds 127.0.0.1 only (the Melody Lab is 8787, the Sound Lab 8788, this is 8789). Renders through
`cosmo-cc mcp` from a COPY of the project, never saves it.

A round is one photo shown N ways, names hidden until asked (choose with the eye, not the label):
  palette  — corrected only, the old cinematic look, and every mood (moods.py) at full strength
  refine   — the pick, and the pick moved one step along each axis: warmer / cooler, magenta / green,
             more contrast / softer, brighter / darker, vivid / muted, faded / deep blacks, the mood
             stronger / lighter. Each refine round halves the step, so it converges on the taste.
The approver marks the BEST (and may mark others good or bad, and say why); the verdict goes to
data/judgments.jsonl with every variant's recipe, and the next round is rendered around the pick.
"""
import argparse, hashlib, html, json, os, random, shutil, sys, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autograde as A  # noqa: E402
import edit as E  # noqa: E402
import looks as L  # noqa: E402
import moods as M  # noqa: E402

DATA = HERE.parent / "data"
JUDGMENTS = DATA / "judgments.jsonl"
DEFAULT_PHOTOS = ["_DSF6613.RAF", "_DSF6686.RAF", "DSCF5186.RAF", "_DSF6332.RAF", "DSCF5411.RAF",
                  "_DSF6999.RAF", "DSCF5807.RAF", "_DSF6904.RAF"]


# ── a variant = (look, strength, tweaks) on top of the photo's correction ───────────────────

def offset_mid(v, d):
    return E.curve_str([(x, y + d * max(0.0, 1.0 - abs(x - 0.5) / 0.5)) for x, y in E.curve_grid(v)])


def _add(d, k, v, base=0.0):
    d[k] = "%g" % (float(d.get(k, base) or base) + v)


TWEAKS = {
    # name: (label, function(d, step))
    "warmer":   ("warmer", lambda d, a: _add(d, "temp", 400 * a, 6500)),
    "cooler":   ("cooler", lambda d, a: _add(d, "temp", -400 * a, 6500)),
    "magenta":  ("more magenta", lambda d, a: _add(d, "tint", 8 * a)),
    "green":    ("more green", lambda d, a: _add(d, "tint", -8 * a)),
    "contrast": ("more contrast", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), -0.03 * a, 0.02 * a))),
    "soft":     ("softer", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), 0.03 * a, -0.03 * a))),
    "brighter": ("brighter", lambda d, a: d.__setitem__("curve", offset_mid(d.get("curve", "0,0;1,1"), 0.05 * a))),
    "darker":   ("darker", lambda d, a: d.__setitem__("curve", offset_mid(d.get("curve", "0,0;1,1"), -0.05 * a))),
    "vivid":    ("more saturated", lambda d, a: _add(d, "saturation", 10 * a)),
    "muted":    ("less saturated", lambda d, a: _add(d, "saturation", -10 * a)),
    "faded":    ("faded blacks", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), 0.05 * a, 0.0))),
    "deep":     ("deeper blacks", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), -0.03 * a, 0.0))),
}
OPPOSITE = {"warmer": "cooler", "cooler": "warmer", "magenta": "green", "green": "magenta", "contrast": "soft",
            "soft": "contrast", "brighter": "darker", "darker": "brighter", "vivid": "muted", "muted": "vivid",
            "faded": "deep", "deep": "faded"}


def look_line(look, fam):
    if look == "corrected":
        return ""
    if look == "cinematic":
        return L.LOOKS["cinematic"]["groups"][fam]["params"]
    return M.MOODS[look][fam]


def look_title(look):
    return {"corrected": "Corrected only", "cinematic": "Cinematic (first grade)"}.get(look) or M.MOODS[look]["title"]


def variant_params(corr, fam, v):
    line = look_line(v["look"], fam)
    d = E.compose(corr, M.scale(line, v["strength"]) if line else "", {})
    for name, step in v["tweaks"]:
        TWEAKS[name][1](d, step)
    return d


def variant_label(v):
    s = look_title(v["look"])
    if v["look"] not in ("corrected",) and abs(v["strength"] - 1.0) > 1e-6:
        s += " %.0f%%" % (v["strength"] * 100)
    if v["tweaks"]:
        s += " · " + ", ".join("%s%s" % (TWEAKS[n][0], "" if abs(a - 1) < 1e-6 else " ×%g" % a) for n, a in v["tweaks"])
    return s


def vid(v):
    return hashlib.sha1(json.dumps(v, sort_keys=True).encode()).hexdigest()[:10]


def simplify(tweaks):
    """Merge repeated tweaks and cancel opposites, so a recipe after six refine rounds stays readable."""
    acc = {}
    for n, a in tweaks:
        o = OPPOSITE[n]
        if acc.get(o):
            take = min(acc[o], a)
            acc[o] -= take
            a -= take
        if a > 1e-6:
            acc[n] = acc.get(n, 0) + a
    return [(n, round(a, 3)) for n, a in acc.items() if a > 1e-6]


# ── the lab ─────────────────────────────────────────────────────────────────────────────────

class Lab:
    def __init__(self, args):
        self.dir = Path(args.lab).expanduser()
        (self.dir / "rounds").mkdir(parents=True, exist_ok=True)
        self.project = self.dir / "lab.cmp"
        if not self.project.exists():
            shutil.copy(Path(args.project).expanduser(), self.project)
        self.plan = json.loads(Path(args.plan).expanduser().read_text())["photos"] if args.plan else {}
        self.lock = threading.Lock()
        self.cs = A.Cosmo(str(self.dir / "cosmo-mcp.log"))
        self.cs.run("project open %s" % self.project)
        self.names = A.nodes_by_name(self.cs.state())
        self.photos = [p for p in (args.photos.split(",") if args.photos else DEFAULT_PHOTOS) if p in self.names] \
            or list(self.names)[:8]
        self.corr = {}            # photo -> (correction dict, family, scene)
        self.state_path = self.dir / "state.json"
        self.state = json.loads(self.state_path.read_text()) if self.state_path.exists() else {"rounds": [], "current": None}
        if not self.state["current"]:
            self.new_round(self.photos[0], "palette", None, 1.0)

    def save_state(self):
        self.state_path.write_text(json.dumps(self.state, indent=1))

    def round(self, rid):
        return json.loads((self.dir / "rounds" / rid / "round.json").read_text())

    def correction(self, photo):
        if photo not in self.corr:
            node = self.names[photo]
            pp = self.plan.get(photo, {})
            r = self.cs.analyze(node, before=True)
            md = A.meta_dict(r["metadata"])
            scene, fam, _ = A.classify(r["analysis"], md)
            if pp.get("scene"):
                scene, fam = pp["scene"], L.FAMILY[pp["scene"]]
            E.clear_masks(self.cs, node)
            self.cs.run("select %d\nset %s" % (node, E.PHOTO_RESET))
            corr, _ = A.normalise(self.cs, node, scene, int(A.number(md.get("ISO"), 100)), [])
            self.corr[photo] = (corr, fam, scene)
        return self.corr[photo]

    def render(self, photo, v, path):
        corr, fam, _ = self.correction(photo)
        node = self.names[photo]
        E.set_photo(self.cs, node, variant_params(corr, fam, v), self.plan.get(photo, {}).get("masks", []))
        self.cs.preview(path, node, edge=1400)

    def new_round(self, photo, kind, base, step):
        if kind == "palette":
            vs = [{"look": "corrected", "strength": 1.0, "tweaks": []},
                  {"look": "cinematic", "strength": 1.0, "tweaks": []}] + \
                 [{"look": k, "strength": 1.0, "tweaks": []} for k in M.MOODS]
        else:
            vs = [dict(base, tweaks=simplify(base["tweaks"]))]
            for n in TWEAKS:
                vs.append(dict(base, tweaks=simplify(base["tweaks"] + [(n, step)])))
            if base["look"] != "corrected":
                vs.append(dict(base, strength=round(min(1.6, base["strength"] + 0.3 * step), 3)))
                vs.append(dict(base, strength=round(max(0.2, base["strength"] - 0.3 * step), 3)))
        uniq, seen = [], set()
        for v in vs:
            if vid(v) not in seen:
                seen.add(vid(v))
                uniq.append(v)
        rid = time.strftime("%Y%m%d-%H%M%S") + "-" + photo.split(".")[0].strip("_")
        rd = self.dir / "rounds" / rid
        rd.mkdir(parents=True, exist_ok=True)
        random.shuffle(uniq)                       # blind: position says nothing
        items = []
        for i, v in enumerate(uniq):
            f = "%02d.jpg" % (i + 1)
            self.render(photo, v, rd / f)
            items.append({"id": vid(v), "file": f, "label": variant_label(v), "variant": v})
        n = sum(1 for r in self.state["rounds"] if r["photo"] == photo)
        rnd = {"id": rid, "photo": photo, "kind": kind, "step": step, "n": n + 1, "scene": self.corr[photo][2],
               "items": items, "judged": False}
        (rd / "round.json").write_text(json.dumps(rnd, indent=1))
        self.state["rounds"].append({"id": rid, "photo": photo, "kind": kind})
        self.state["current"] = rid
        self.save_state()
        return rnd

    def judge(self, body):
        with self.lock:
            rnd = self.round(body["round"])
            by = {it["id"]: it for it in rnd["items"]}
            best = by.get(body.get("best"))
            rec = {"at": time.strftime("%Y-%m-%dT%H:%M:%S"), "photo": rnd["photo"], "scene": rnd["scene"],
                   "round": rnd["id"], "kind": rnd["kind"], "step": rnd["step"],
                   "best": best and {"label": best["label"], **best["variant"]},
                   "good": [{"label": by[i]["label"], **by[i]["variant"]} for i in body.get("good", []) if i in by],
                   "bad": [{"label": by[i]["label"], **by[i]["variant"]} for i in body.get("bad", []) if i in by],
                   "note": body.get("note", ""), "action": body.get("action", "refine"),
                   "shown": [it["label"] for it in rnd["items"]]}
            DATA.mkdir(parents=True, exist_ok=True)
            with open(JUDGMENTS, "a") as f:
                f.write(json.dumps(rec) + "\n")
            rnd["judged"] = True
            rnd["verdict"] = {"best": body.get("best"), "good": body.get("good", []), "bad": body.get("bad", []),
                              "note": body.get("note", "")}
            (self.dir / "rounds" / rnd["id"] / "round.json").write_text(json.dumps(rnd, indent=1))
            action = body.get("action", "refine")
            if action == "refine" and best:
                return self.new_round(rnd["photo"], "refine", best["variant"],
                                      1.0 if rnd["kind"] == "palette" else max(0.25, rnd["step"] / 2))
            if action == "photo" and body.get("photo") in self.photos:
                return self.new_round(body["photo"], "palette", None, 1.0)
            i = self.photos.index(rnd["photo"]) if rnd["photo"] in self.photos else -1
            return self.new_round(self.photos[(i + 1) % len(self.photos)], "palette", None, 1.0)


def summary_text():
    if not JUDGMENTS.exists():
        return "no verdicts yet"
    recs = [json.loads(l) for l in JUDGMENTS.read_text().splitlines() if l.strip()]
    recs = [r for r in recs if r.get("best") and "look" in r["best"]]
    if not recs:
        return "no Grade Lab verdicts yet"
    out = ["%d verdicts on %d photos" % (len(recs), len({r["photo"] for r in recs}))]
    looks = {}
    for r in recs:
        if r["kind"] == "palette":
            looks[r["best"]["look"]] = looks.get(r["best"]["look"], 0) + 1
    out.append("palette picks: " + (", ".join("%s %d" % (look_title(k), n) for k, n in
                                                sorted(looks.items(), key=lambda x: -x[1])) or "none"))
    pref = {}
    for r in recs:
        if r["kind"] != "refine":
            continue
        for n, a in r["best"]["tweaks"]:
            pref[n] = pref.get(n, 0) + a
    if pref:
        out.append("refine leans (summed steps of the picks): " +
                   ", ".join("%s %+.2f" % (TWEAKS[n][0], a) for n, a in sorted(pref.items(), key=lambda x: -x[1])))
    strengths = [r["best"]["strength"] for r in recs if r["best"]["look"] not in ("corrected",)]
    if strengths:
        out.append("mood strength of the picks: mean %.2f (%d)" % (sum(strengths) / len(strengths), len(strengths)))
    notes = [("%s: %s" % (r["photo"], r["note"])) for r in recs if r.get("note")]
    if notes:
        out.append("notes:\n  " + "\n  ".join(notes))
    return "\n".join(out)


# ── the page ────────────────────────────────────────────────────────────────────────────────

PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>Grade Lab</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#121214;--card:#1c1c20;--ink:#ececf0;--dim:#9a9aa6;--acc:#e8a0b8;--good:#7fc8a0;--bad:#d77;}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.45 system-ui,sans-serif}
header{position:sticky;top:0;z-index:5;background:#121214ee;backdrop-filter:blur(6px);padding:10px 16px;border-bottom:1px solid #2a2a30;display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center}
h1{font-size:17px;margin:0}h1 small{color:var(--dim);font-weight:400}
.photos{display:flex;gap:6px;flex-wrap:wrap}.photos button{background:#25252b;color:var(--ink);border:1px solid #33333b;border-radius:6px;padding:4px 8px;cursor:pointer;font-size:12px}
.photos button.on{border-color:var(--acc);color:var(--acc)}
main{padding:14px 16px 140px}.meta{color:var(--dim);margin:0 0 10px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:12px}
.card{background:var(--card);border:2px solid transparent;border-radius:10px;overflow:hidden}
.card.best{border-color:var(--acc)}.card.good{border-color:var(--good)}.card.bad{opacity:.45}
.card img{width:100%;display:block;cursor:zoom-in;background:#000}
.row{display:flex;gap:6px;padding:8px;align-items:center;flex-wrap:wrap}
.row button{flex:1;background:#27272e;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:6px;cursor:pointer}
.row button.on.b{background:var(--acc);color:#222}.row button.on.g{background:var(--good);color:#122}.row button.on.x{background:var(--bad);color:#200}
.label{padding:0 8px 8px;color:var(--dim);font-size:12px;min-height:8px}
footer{position:fixed;bottom:0;left:0;right:0;background:#17171bf2;border-top:1px solid #2a2a30;padding:10px 16px;display:flex;gap:10px;align-items:center;flex-wrap:wrap}
footer textarea{flex:1;min-width:240px;height:44px;background:#202026;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:6px}
footer button{background:var(--acc);color:#222;border:0;border-radius:6px;padding:10px 14px;font-weight:600;cursor:pointer}
footer button.alt{background:#2c2c33;color:var(--ink)}footer label{color:var(--dim)}
#view{position:fixed;inset:0;background:#000e;display:none;z-index:9;align-items:center;justify-content:center;flex-direction:column}
#view img{max-width:96vw;max-height:88vh}#view p{color:var(--dim);margin:8px}
#busy{display:none;position:fixed;inset:0;background:#000b;z-index:10;align-items:center;justify-content:center;font-size:18px}
</style></head><body>
<header><h1>Grade Lab <small>arstro.color.grading — you are the approver</small></h1>
<div class="photos" id="photos"></div><label><input type="checkbox" id="names"> show names</label>
<a href="/taste" style="color:var(--acc)">taste so far</a></header>
<main><p class="meta" id="meta"></p><div class="grid" id="grid"></div></main>
<footer><textarea id="note" placeholder="why? (optional) — e.g. 'skin too pink', 'love the mint shadows', 'sky too flat'"></textarea>
<button id="refine">Pick best → refine around it</button><button class="alt" id="next">Pick best → next photo</button></footer>
<div id="view"><img id="vimg"><p id="vlab">← → to move · B best · G good · X bad · Esc close</p></div>
<div id="busy">rendering the next round…</div>
<script>
let R=null, verdict={best:null,good:new Set(),bad:new Set()}, vi=0;
const $=s=>document.querySelector(s);
async function load(){R=await (await fetch('/api/round')).json();verdict={best:null,good:new Set(),bad:new Set()};draw();}
function draw(){
  $('#photos').innerHTML=R.photos.map(p=>`<button class="${p==R.photo?'on':''}" data-p="${p}">${p.replace('.RAF','')}</button>`).join('');
  document.querySelectorAll('.photos button').forEach(b=>b.onclick=()=>go('photo',b.dataset.p));
  $('#meta').textContent=`${R.photo} · ${R.scene} · round ${R.n} — ${R.kind=='palette'?'which look? (the moods, names hidden)':'refine: the pick and one step each way (step '+R.step+')'} · ${R.items.length} versions`;
  $('#grid').innerHTML=R.items.map((it,i)=>`<div class="card" id="c${i}"><img src="/img/${R.id}/${it.file}" data-i="${i}" loading="lazy">
   <div class="row"><button class="b" data-a="best" data-i="${i}">Best</button><button class="g" data-a="good" data-i="${i}">Good</button><button class="x" data-a="bad" data-i="${i}">Bad</button></div>
   <div class="label">${$('#names').checked?it.label:'#'+(i+1)}</div></div>`).join('');
  document.querySelectorAll('.card img').forEach(im=>im.onclick=()=>open_(+im.dataset.i));
  document.querySelectorAll('.row button').forEach(b=>b.onclick=()=>mark(b.dataset.a,+b.dataset.i));
  paint();
}
function mark(a,i){const id=R.items[i].id;
  if(a=='best'){verdict.best=verdict.best==id?null:id;verdict.bad.delete(id);}
  else{const s=verdict[a],o=verdict[a=='good'?'bad':'good'];s.has(id)?s.delete(id):(s.add(id),o.delete(id));if(a=='bad'&&verdict.best==id)verdict.best=null;}
  paint();}
function paint(){R.items.forEach((it,i)=>{const c=$('#c'+i);c.className='card'+(verdict.best==it.id?' best':verdict.good.has(it.id)?' good':verdict.bad.has(it.id)?' bad':'');
  c.querySelectorAll('.row button').forEach(b=>b.classList.toggle('on',(b.dataset.a=='best'&&verdict.best==it.id)||(b.dataset.a=='good'&&verdict.good.has(it.id))||(b.dataset.a=='bad'&&verdict.bad.has(it.id))));});}
function open_(i){vi=i;$('#vimg').src=`/img/${R.id}/${R.items[i].file}`;$('#vlab').textContent=`#${i+1}${$('#names').checked?' — '+R.items[i].label:''} · ← → move · B best · G good · X bad · Esc close`;$('#view').style.display='flex';}
$('#view').onclick=e=>{if(e.target.id=='view')$('#view').style.display='none';};
document.onkeydown=e=>{if($('#view').style.display!='flex')return;
  if(e.key=='Escape')$('#view').style.display='none';
  if(e.key=='ArrowRight')open_((vi+1)%R.items.length);if(e.key=='ArrowLeft')open_((vi-1+R.items.length)%R.items.length);
  if(e.key=='b'||e.key=='B'){mark('best',vi);open_(vi);}if(e.key=='g'||e.key=='G'){mark('good',vi);open_(vi);}if(e.key=='x'||e.key=='X'){mark('bad',vi);open_(vi);}};
$('#names').onchange=draw;
async function go(action,photo){
  if(action!='photo'&&!verdict.best){alert('Mark the best one first (click Best under a photo, or B in the viewer).');return;}
  $('#busy').style.display='flex';
  await fetch('/api/judge',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({round:R.id,best:verdict.best,good:[...verdict.good],bad:[...verdict.bad],note:$('#note').value,action,photo})});
  $('#note').value='';$('#busy').style.display='none';window.scrollTo(0,0);load();}
$('#refine').onclick=()=>go('refine');$('#next').onclick=()=>go('next');
load();
</script></body></html>"""


def serve(args):
    lab = Lab(args)

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype):
            b = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("content-type", ctype)
            self.send_header("cache-control", "no-store")
            self.send_header("content-length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)

        def do_GET(self):
            u = urllib.parse.urlparse(self.path).path
            if u == "/":
                return self.send(200, PAGE, "text/html; charset=utf-8")
            if u == "/api/round":
                r = lab.round(lab.state["current"])
                r["photos"] = lab.photos
                return self.send(200, json.dumps(r), "application/json")
            if u == "/taste":
                return self.send(200, "<pre style='font:14px/1.5 monospace;padding:16px;white-space:pre-wrap'>%s</pre>"
                                 % html.escape(summary_text()), "text/html; charset=utf-8")
            if u.startswith("/img/"):
                parts = u.split("/")
                p = (lab.dir / "rounds" / parts[2] / parts[3]).resolve()
                if p.is_file() and lab.dir.resolve() in p.parents:
                    return self.send(200, p.read_bytes(), "image/jpeg")
            self.send(404, "not found", "text/plain")

        def do_POST(self):
            if self.path != "/api/judge":
                return self.send(404, "not found", "text/plain")
            n = int(self.headers.get("content-length", 0))
            body = json.loads(self.rfile.read(n) or b"{}")
            if body.get("action") == "photo":
                with lab.lock:
                    lab.new_round(body["photo"], "palette", None, 1.0)
                return self.send(200, "{}", "application/json")
            lab.judge(body)
            self.send(200, "{}", "application/json")

    httpd = ThreadingHTTPServer(("127.0.0.1", args.port), H)
    print("Grade Lab on http://127.0.0.1:%d  (lab %s, verdicts -> %s)" % (args.port, lab.dir, JUDGMENTS), flush=True)
    httpd.serve_forever()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("serve", "summary"))
    ap.add_argument("--project", default=os.path.expanduser("~/photo/tokyo/cinematic-grade/tokyo.before-grade.cmp"))
    ap.add_argument("--plan", default="")
    ap.add_argument("--photos", default="")
    ap.add_argument("--port", type=int, default=8789)
    ap.add_argument("--lab", default=os.path.expanduser("~/photo/grade-lab"))
    args = ap.parse_args()
    if args.cmd == "summary":
        print(summary_text())
    else:
        serve(args)


if __name__ == "__main__":
    main()
