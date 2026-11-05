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
import argparse, hashlib, html, json, os, random, re, shutil, sys, threading, time, urllib.parse
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
# Taste rounds go to photos the approver has NOT judged yet — a taste that only fits the photos it was
# learned on is not a taste.
TASTE_PHOTOS = ["DSCF5288.RAF", "_DSF5417.RAF", "_DSF6151.RAF", "_DSF6609.RAF", "_DSF6994.RAF", "DSCF5194.RAF",
                "_DSF6696 (1).RAF", "_DSF6778.RAF", "_DSF6592.RAF", "_DSF7014.RAF"]
# A taste round tests what the verdicts said (taste.md, 2026-10-30): the winner, the refine pick, the two
# looks built from the verdicts, and milder versions of the moods that won on landscapes.
# Taste round v2 (2026-11-02): the profile's own picks and their nearest rivals, every one at FULL depth now
# (strength scales colour only) — Moody Forest is gone: matte is flat, and flat lost every time it was shown.
TASTE_CANDIDATES = [
    {"look": "natural-teal", "strength": 1.0, "tweaks": []},
    {"look": "natural-teal", "strength": 0.7, "tweaks": []},
    {"look": "natural-cinema", "strength": 1.0, "tweaks": []},
    {"look": "natural-cinema", "strength": 0.7, "tweaks": []},
    {"look": "golden-dream", "strength": 0.5, "tweaks": []},
    {"look": "neon-noir", "strength": 0.6, "tweaks": []},
    {"look": "teal-orange", "strength": 0.6, "tweaks": []},
    {"look": "cinematic", "strength": 1.0, "tweaks": [["warmer", 1.0]]},
    {"look": "natural-teal", "strength": 1.0, "tweaks": [["warmer", 1.0]]},
    {"look": "corrected", "strength": 1.0, "tweaks": []},
]
# Photos the approver asked to take out of the judging ("next time remove this photo, it's too bad to judge").
REMOVED = ["_DSF6778.RAF", "_DSF7014.RAF"]


# ── a variant = (look, strength, tweaks) on top of the photo's correction ───────────────────
# The steps, the looks and the composition are taste.py's, shared with the editor — what the approver
# picks here is exactly what `edit.py run --mood taste` can reproduce.
from taste import TWEAKS, OPPOSITE, simplify, look_line, look_title  # noqa: E402
import taste as T  # noqa: E402


def variant_params(corr, fam, v):
    return T.params(corr, fam, v)


def variant_label(v):
    s = look_title(v["look"])
    if v["look"] not in ("corrected",) and abs(v["strength"] - 1.0) > 1e-6:
        s += " %.0f%%" % (v["strength"] * 100)
    if v["tweaks"]:
        s += " · " + ", ".join("%s%s" % (TWEAKS[n][0], "" if abs(a - 1) < 1e-6 else " ×%g" % a) for n, a in v["tweaks"])
    return s


def vid(v):
    return hashlib.sha1(json.dumps(v, sort_keys=True).encode()).hexdigest()[:10]


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
        order = args.photos.split(",") if args.photos else TASTE_PHOTOS + DEFAULT_PHOTOS
        self.photos = [p for p in order if p in self.names] or list(self.names)[:8]
        self.photos += [p for p in self.names if p not in self.photos]
        self.photos = [p for p in self.photos if p not in REMOVED]
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

    def render(self, photo, v, path, kind="taste"):
        corr, fam, _ = self.correction(photo)
        node = self.names[photo]
        pe = self.plan.get(photo, {})
        # The basic mechanism ("keep the skin color for human") holds in EVERY version of a photo with a person.
        v = dict(v, skin=T.has_person({}, pe) if pe.get("subject") else False)
        d = variant_params(corr, fam, v)
        E.set_photo(self.cs, node, d, pe.get("masks", []))
        if kind != "refine":     # what the editor would deliver: no flat version is shown as a candidate
            E.depth(self.cs, node, d, fam, [])
        # The same clipping guard the editor runs: the first taste round showed a look burning the teamLab
        # fire to lime because the lab rendered without it ("make sure this mistake doesn't happen").
        E.guard(self.cs, node, d, fam, [])
        self.cs.preview(path, node, edge=1400)

    def new_round(self, photo, kind, base, step):
        if kind == "taste":
            vs = [dict(v) for v in TASTE_CANDIDATES]
        elif kind == "palette":
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
        # Only URL-safe characters: "_DSF6696 (1).RAF" made a folder the page could not load ("this page is broken").
        rid = time.strftime("%Y%m%d-%H%M%S") + "-" + re.sub(r"[^A-Za-z0-9]+", "-", photo.rsplit(".", 1)[0]).strip("-")
        rd = self.dir / "rounds" / rid
        rd.mkdir(parents=True, exist_ok=True)
        random.shuffle(uniq)                       # blind: position says nothing
        items = []
        for i, v in enumerate(uniq):
            f = "%02d.jpg" % (i + 1)
            self.render(photo, v, rd / f, kind)
            items.append({"id": vid(v), "file": f, "label": variant_label(v), "variant": v})
        n = sum(1 for r in self.state["rounds"] if r["photo"] == photo)
        rnd = {"id": rid, "photo": photo, "kind": kind, "step": step, "n": n + 1, "scene": self.corr[photo][2],
               "items": items, "judged": False}
        (rd / "round.json").write_text(json.dumps(rnd, indent=1))
        self.state["rounds"].append({"id": rid, "photo": photo, "kind": kind})
        self.state["current"] = rid
        self.save_state()
        return rnd

    def mark(self, body):
        """Every click is saved the moment it is made (a DRAFT on the round), so a mark is never lost to a
        missed button: on 2026-11-02 a whole rating session vanished because marks lived only in the page
        until a "Pick best" button was pressed, and clicking a photo name discarded them."""
        with self.lock:
            rp = self.dir / "rounds" / body["round"] / "round.json"
            rnd = json.loads(rp.read_text())
            rnd["draft"] = {"best": body.get("best"), "good": body.get("good", []), "bad": body.get("bad", []),
                            "note": body.get("note", ""), "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
            rp.write_text(json.dumps(rnd, indent=1))

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
                return self.new_round(body["photo"], "taste" if self.state.get("mode") == "taste" else "palette", None, 1.0)
            i = self.photos.index(rnd["photo"]) if rnd["photo"] in self.photos else -1
            # A taste session stays a taste session: the next photo gets the same candidates.
            nxt = "taste" if rnd["kind"] in ("taste",) or self.state.get("mode") == "taste" else "palette"
            return self.new_round(self.photos[(i + 1) % len(self.photos)], nxt, None, 1.0)


def drafts(lab_dir=os.path.expanduser("~/photo/grade-lab")):
    """Marks saved on rounds that never got a verdict (the approver moved on without pressing a button) —
    they are the approver's picks all the same, so the analysis reads them, flagged as drafts."""
    out = []
    for rp in sorted(Path(lab_dir).glob("rounds/*/round.json")):
        r = json.loads(rp.read_text())
        d = r.get("draft") or {}
        if r.get("judged") or not d.get("best"):
            continue
        by = {it["id"]: it for it in r["items"]}
        b = by.get(d["best"])
        if not b:
            continue
        out.append({"at": d.get("at"), "photo": r["photo"], "scene": r.get("scene"), "round": r["id"], "kind": r["kind"],
                    "step": r["step"], "best": {"label": b["label"], **b["variant"]},
                    "good": [{"label": by[i]["label"], **by[i]["variant"]} for i in d.get("good", []) if i in by],
                    "bad": [{"label": by[i]["label"], **by[i]["variant"]} for i in d.get("bad", []) if i in by],
                    "note": d.get("note", ""), "action": "draft", "shown": [it["label"] for it in r["items"]]})
    return out


def summary_text():
    if not JUDGMENTS.exists():
        return "no verdicts yet"
    recs = [json.loads(l) for l in JUDGMENTS.read_text().splitlines() if l.strip()] + drafts()
    styles = [r for r in recs if r.get("kind") == "style"]
    recs = [r for r in recs if r.get("best") and "look" in r["best"] and r.get("kind") != "style"]   # exam lines have no best: exam.py score
    if not recs:
        return "no Grade Lab verdicts yet"
    out = ["%d verdicts on %d photos" % (len(recs), len({r["photo"] for r in recs}))]
    looks = {}
    for r in recs:
        if r["kind"] in ("palette", "taste"):
            key = r["best"]["look"] + ("" if abs(r["best"]["strength"] - 1) < 1e-6 else " %.0f%%" % (r["best"]["strength"] * 100))
            looks[key] = looks.get(key, 0) + 1
    out.append("look picks (palette + taste rounds): " + (", ".join("%s %d" % (look_title(k.split(" ")[0]) + k[len(k.split(" ")[0]):], n) for k, n in
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
    if styles:
        last = {}
        for r in styles:
            last[r["style"]] = r
        out.append("style picks (Style Lab): " + ", ".join(
            "%s %s" % (k, "none fits" if r["none"] else r["best"]["letter"]) for k, r in last.items()))
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
<a href="/taste" style="color:var(--acc)">taste so far</a><span id="saved" style="color:var(--good)"></span></header>
<main><p class="meta" id="meta"></p><div class="grid" id="grid"></div></main>
<footer><textarea id="note" placeholder="why? (optional) — e.g. 'skin too pink', 'love the mint shadows', 'sky too flat'"></textarea>
<button id="refine">Pick best → refine around it</button><button class="alt" id="next">Pick best → next photo</button></footer>
<div id="view"><img id="vimg"><p id="vlab">← → to move · B best · G good · X bad · Esc close</p></div>
<div id="busy">rendering the next round…</div>
<script>
let R=null, verdict={best:null,good:new Set(),bad:new Set()}, vi=0;
const $=s=>document.querySelector(s);
async function load(){R=await (await fetch('/api/round')).json();const d=R.draft||{};
  verdict={best:d.best||null,good:new Set(d.good||[]),bad:new Set(d.bad||[])};$('#note').value=d.note||'';draw();
  $('#saved').textContent=d.best?'saved ✓ (your marks on this photo)':'';}
async function save(){try{await fetch('/api/mark',{method:'POST',headers:{'content-type':'application/json'},
  body:JSON.stringify({round:R.id,best:verdict.best,good:[...verdict.good],bad:[...verdict.bad],note:$('#note').value})});
  $('#saved').textContent='saved ✓';}catch(e){$('#saved').textContent='NOT saved — is the lab running?';$('#saved').style.color='var(--bad)';}}
function draw(){
  $('#photos').innerHTML=R.photos.map(p=>`<button class="${p==R.photo?'on':''}" data-p="${p}">${p.replace('.RAF','')}</button>`).join('');
  document.querySelectorAll('.photos button').forEach(b=>b.onclick=()=>go('photo',b.dataset.p));
  $('#meta').textContent=`${R.photo} · ${R.scene} · round ${R.n} — ${R.kind=='palette'?'which look? (the moods, names hidden)':R.kind=='taste'?'taste check: what I learned from your picks, on a photo you have not judged':'refine: the pick and one step each way (step '+R.step+')'} · ${R.items.length} versions`;
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
  paint();save();}
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
  await save();   // a photo switch keeps the marks: with a Best they become the verdict, without one they stay a draft
  $('#busy').style.display='flex';
  await fetch('/api/judge',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({round:R.id,best:verdict.best,good:[...verdict.good],bad:[...verdict.bad],note:$('#note').value,action,photo})});
  $('#note').value='';$('#busy').style.display='none';window.scrollTo(0,0);load();}
$('#note').onchange=save;
$('#refine').onclick=()=>go('refine');$('#next').onclick=()=>go('next');
load();
</script></body></html>"""


EXAM_PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>Grade Exam</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#121214;--card:#1b1b1f;--ink:#ececf0;--dim:#9a9aa6;--acc:#e8a0b8;--good:#7fc8a0;--bad:#e07a7a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,sans-serif}
header{position:sticky;top:0;z-index:5;background:#121214f0;padding:10px 16px;border-bottom:1px solid #2a2a30;display:flex;gap:18px;align-items:center;flex-wrap:wrap}
h1{font-size:17px;margin:0}h1 small{color:var(--dim);font-weight:400}#score{font-weight:600}#score.pass{color:var(--good)}#score.fail{color:var(--acc)}
main{padding:14px 16px 60px;max-width:1500px;margin:auto}
.card{background:var(--card);border-radius:12px;margin:0 0 22px;overflow:hidden;border:2px solid transparent}
.card.good{border-color:var(--good)}.card.bad{border-color:var(--bad)}
.pair{display:grid;grid-template-columns:1fr 1fr;gap:6px;background:#000}
.pair figure{margin:0;position:relative}.pair img{width:100%;display:block;cursor:zoom-in}
.pair figcaption{position:absolute;left:8px;top:8px;background:#000a;padding:2px 8px;border-radius:4px;font-size:12px}
.body{padding:12px 16px}.name{color:var(--dim);font-size:12px}
.reading{font-size:15px;margin:6px 0 4px}.light{color:var(--dim);margin:0 0 8px}
ul{margin:6px 0 10px;padding-left:18px}li{margin:3px 0}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.row button{background:#27272e;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:8px 16px;cursor:pointer;font-weight:600}
.row button.on.g{background:var(--good);color:#122}.row button.on.b{background:var(--bad);color:#200}
.row input{flex:1;min-width:240px;background:#202026;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:8px}
.saved{color:var(--good);font-size:12px}
#view{position:fixed;inset:0;background:#000e;display:none;z-index:9;align-items:center;justify-content:center}
#view img{max-width:98vw;max-height:96vh}
@media(max-width:800px){.pair{grid-template-columns:1fr}}
</style></head><body>
<header><h1>Grade Exam <small>one edit per photo, decided by the skill — is it good?</small></h1><span id="score"></span>
<a href="/" style="color:var(--acc)">back to the lab</a></header>
<main id="m"></main><div id="view"><img id="vimg"></div>
<script>
let E=null;const $=s=>document.querySelector(s);
const md=t=>t.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/\*\*(.+?)\*\*/g,'<b>$1</b>');
const SET=new URLSearchParams(location.search).get('set')||'';const Q=SET?'?set='+encodeURIComponent(SET):'';
async function load(){E=await (await fetch('/api/exam'+Q)).json();if(SET)document.title='Grade - '+SET;draw();}
function draw(){
 $('#m').innerHTML=E.photos.map((p,i)=>`<div class="card ${p.verdict?(p.verdict.good?'good':'bad'):''}" id="c${i}">
  <div class="pair"><figure><img src="/exam-img/${p.before}${Q}" data-s="/exam-img/${p.before}${Q}"><figcaption>original (as shot)</figcaption></figure>
  <figure><img src="/exam-img/${p.after}${Q}" data-s="/exam-img/${p.after}${Q}"><figcaption>the skill's edit</figcaption></figure></div>
  <div class="body"><div class="name">${i+1} / ${E.photos.length} · ${p.name}</div>
  <p class="reading"><b>What I see:</b> ${md(p.reading||'')}</p><p class="light">${md(p.light||'')}</p>
  <ul>${p.steps.map(x=>'<li>'+md(x)+'</li>').join('')}</ul>
  <div class="row"><button class="g ${p.verdict&&p.verdict.good?'on':''}" data-i="${i}" data-v="1">Good</button>
  <button class="b ${p.verdict&&!p.verdict.good?'on':''}" data-i="${i}" data-v="0">Not good</button>
  <input placeholder="why? (what is wrong, or what is right)" value="${(p.verdict&&p.verdict.note||'').replace(/"/g,'&quot;')}" data-i="${i}">
  <span class="saved" id="s${i}">${p.verdict?'saved ✓':''}</span></div></div></div>`).join('');
 document.querySelectorAll('.row button').forEach(b=>b.onclick=()=>judge(+b.dataset.i,b.dataset.v=='1'));
 document.querySelectorAll('.row input').forEach(t=>t.onchange=()=>{const p=E.photos[+t.dataset.i];if(p.verdict)judge(+t.dataset.i,p.verdict.good);});
 document.querySelectorAll('.pair img').forEach(im=>im.onclick=()=>{$('#vimg').src=im.dataset.s;$('#view').style.display='flex';});
 score();}
$('#view').onclick=()=>$('#view').style.display='none';
function score(){const j=E.photos.filter(p=>p.verdict),g=j.filter(p=>p.verdict.good).length,pc=j.length?Math.round(100*g/j.length):0;
 $('#score').textContent=`${g} good of ${j.length} judged (${E.photos.length} photos) — ${pc}% · target 90%`;
 $('#score').className=j.length?(pc>=90?'pass':'fail'):'';}
async function judge(i,good){const p=E.photos[i],note=document.querySelector(`.row input[data-i="${i}"]`).value;
 p.verdict={good,note};const r=await fetch('/api/exam-judge'+Q,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({name:p.name,good,note})});
 const c=$('#c'+i);c.className='card '+(good?'good':'bad');c.querySelectorAll('.row button').forEach(b=>b.classList.toggle('on',(b.dataset.v=='1')==good));
 $('#s'+i).textContent=r.ok?'saved ✓':'NOT saved';score();}
load();
</script></body></html>"""


STYLES_PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>Style Lab</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#121214;--card:#1b1b1f;--ink:#ececf0;--dim:#9a9aa6;--acc:#e8a0b8;--good:#7fc8a0;--bad:#e07a7a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 system-ui,sans-serif}
header{position:sticky;top:0;z-index:5;background:#121214f0;padding:10px 16px;border-bottom:1px solid #2a2a30;display:flex;gap:16px;align-items:center;flex-wrap:wrap}
h1{font-size:17px;margin:0}h1 small{color:var(--dim);font-weight:400}#prog{font-weight:600;color:var(--acc)}
header label{color:var(--dim)}header a{color:var(--acc)}
main{padding:14px 16px 60px;max-width:1700px;margin:auto}
.intro{color:var(--dim);margin:0 0 16px;max-width:900px}
.card{background:var(--card);border-radius:12px;margin:0 0 26px;overflow:hidden;border:2px solid transparent}
.card.done{border-color:var(--good)}.card.none{border-color:var(--bad)}
.head{padding:12px 16px 8px}.head h2{margin:0;font-size:19px}.head h2 span{color:var(--dim);font-size:13px;font-weight:400}
.means{margin:2px 0}.words{color:var(--dim);font-size:13px}.words b{color:var(--ink);font-weight:500}
.row4{display:grid;grid-template-columns:repeat(4,1fr);gap:6px;background:#000;padding:6px}
@media(max-width:1000px){.row4{grid-template-columns:repeat(2,1fr)}}
.c{position:relative;background:#0b0b0d;border:2px solid transparent;border-radius:6px;overflow:hidden}
.c.best{border-color:var(--acc)}.c.wrong{opacity:.42}
.c img{width:100%;display:block;cursor:zoom-in;user-select:none;-webkit-user-drag:none}
.c .tag{position:absolute;left:6px;top:6px;background:#000b;padding:1px 8px;border-radius:4px;font-size:12px}
.c .btns{display:flex;gap:6px;padding:6px}
.c .btns button{flex:1;background:#27272e;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:7px;cursor:pointer;font-weight:600}
.c .btns button.on.b{background:var(--acc);color:#222}.c .btns button.on.x{background:var(--bad);color:#200}
.c .lab{padding:0 8px 8px;color:var(--dim);font-size:12px;display:none}.reveal .c .lab{display:block}
.orig .btns{color:var(--dim);font-size:12px;padding:10px 8px;justify-content:center}
.foot{display:flex;gap:10px;align-items:center;flex-wrap:wrap;padding:10px 16px 14px}
.foot input{flex:1;min-width:260px;background:#202026;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:8px}
.foot button{background:#2c2c33;color:var(--ink);border:1px solid #383840;border-radius:6px;padding:8px 12px;cursor:pointer}
.foot button.on{background:var(--bad);color:#200}.saved{color:var(--good);font-size:12px}
#view{position:fixed;inset:0;background:#000e;display:none;z-index:9;align-items:center;justify-content:center;flex-direction:column}
#view img{max-width:98vw;max-height:90vh}#view p{color:var(--dim);margin:8px}
</style></head><body>
<header><h1>Style Lab <small>arstro.color.grading.style — what does each word mean to you?</small></h1>
<span id="prog"></span><label><input type="checkbox" id="rev"> show what each version is</label>
<a href="/">Grade Lab</a><a href="/exam">exam</a></header>
<main><p class="intro">Each card is one STYLE a user may ask for, with the words people use for it. The three versions are three
readings of that word on the same photo (order shuffled). Mark the one that <b>is</b> the word — <b>Best</b> — and any that are
<b>Wrong</b> for it; or "none of them" if no version says the word. Press and hold a photo to see the original. Your picks
become the recipe an agent uses when a user writes that word.</p><div id="m"></div></main>
<div id="view"><img id="vimg"><p id="vlab"></p></div>
<script>
let B=null;const $=s=>document.querySelector(s);
const esc=t=>(t||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');
async function load(){B=await (await fetch('/api/styles')).json();draw();}
function V(s){return s.verdict||(s.verdict={best:null,wrong:[],none:false,note:''});}
function draw(){
 $('#m').innerHTML=B.styles.map((s,i)=>{const v=V(s);return `<div class="card ${v.none?'none':v.best?'done':''}" id="s${i}">
  <div class="head"><h2>${esc(s.title)} <span>· ${esc(s.photo)}${s.subject?' — '+esc(s.subject):''}</span></h2>
  <p class="means">${esc(s.means)}</p><p class="words"><b>people say:</b> ${s.words.map(esc).join(' · ')}</p></div>
  <div class="row4"><div class="c orig"><img src="/styles-img/${s.id}/${s.original}" data-s="${i}" data-k="-1"><span class="tag">original (as shot)</span>
   <div class="btns">the camera's frame</div></div>
  ${s.candidates.map((c,k)=>`<div class="c ${v.best==c.letter?'best':''} ${v.wrong.includes(c.letter)?'wrong':''}" id="c${i}_${k}">
   <img src="/styles-img/${s.id}/${c.file}" data-s="${i}" data-k="${k}"><span class="tag">#${k+1}</span>
   <div class="btns"><button class="b ${v.best==c.letter?'on':''}" data-s="${i}" data-k="${k}" data-a="best">Best</button>
   <button class="x ${v.wrong.includes(c.letter)?'on':''}" data-s="${i}" data-k="${k}" data-a="wrong">Wrong</button></div>
   <div class="lab">${esc(c.label)}</div></div>`).join('')}</div>
  <div class="foot"><button class="${v.none?'on':''}" data-none="${i}">None of them is "${esc(s.title)}"</button>
  <input placeholder="why? (optional) — e.g. 'too orange', '#2 is warm but the sky went yellow'" value="${esc(v.note)}" data-n="${i}">
  <span class="saved" id="ok${i}">${s.verdict.at?'saved ✓':''}</span></div></div>`;}).join('');
 document.querySelectorAll('.c .btns button').forEach(b=>b.onclick=()=>mark(+b.dataset.s,+b.dataset.k,b.dataset.a));
 document.querySelectorAll('[data-none]').forEach(b=>b.onclick=()=>{const v=V(B.styles[+b.dataset.none]);v.none=!v.none;if(v.none)v.best=null;save(+b.dataset.none);});
 document.querySelectorAll('[data-n]').forEach(t=>t.onchange=()=>{V(B.styles[+t.dataset.n]).note=t.value;save(+t.dataset.n);});
 document.querySelectorAll('.c img').forEach(im=>{const s=B.styles[+im.dataset.s],k=+im.dataset.k;
  const orig=`/styles-img/${s.id}/${s.original}`,own=im.src;let held=false,t=null;
  if(k>=0){im.onpointerdown=e=>{t=setTimeout(()=>{held=true;im.src=orig;},180);};
   im.onpointerup=im.onpointerleave=()=>{clearTimeout(t);if(held){im.src=own;setTimeout(()=>held=false,0);}};}
  im.onclick=()=>{if(!held)open_(+im.dataset.s,k);};});
 prog();}
function prog(){const n=B.styles.filter(s=>s.verdict&&(s.verdict.best||s.verdict.none)).length;$('#prog').textContent=`${n} of ${B.styles.length} styles judged`;}
function mark(i,k,a){const s=B.styles[i],v=V(s),L=s.candidates[k].letter;
 if(a=='best'){v.best=v.best==L?null:L;v.wrong=v.wrong.filter(x=>x!=L);if(v.best)v.none=false;}
 else{v.wrong.includes(L)?v.wrong=v.wrong.filter(x=>x!=L):(v.wrong.push(L),v.best==L&&(v.best=null));}
 save(i);}
async function save(i){const s=B.styles[i],v=V(s);v.note=document.querySelector(`[data-n="${i}"]`).value;
 const r=await fetch('/api/style-judge',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify({style:s.id,best:v.best,wrong:v.wrong,none:v.none,note:v.note})});
 v.at=r.ok?'now':'';const y=window.scrollY;draw();window.scrollTo(0,y);$('#ok'+i).textContent=r.ok?'saved ✓':'NOT saved — is the lab running?';}
let vs=0,vk=0;
function open_(i,k){vs=i;vk=k;const s=B.styles[i],c=k<0?null:s.candidates[k];
 $('#vimg').src=`/styles-img/${s.id}/${c?c.file:s.original}`;
 $('#vlab').textContent=`${s.title} — ${c?'#'+(k+1)+($('#rev').checked?' · '+c.label:''):'original'} · ← → move · B best · X wrong · O original (hold) · Esc close`;
 $('#view').style.display='flex';}
$('#view').onclick=e=>{if(e.target.id=='view')$('#view').style.display='none';};
document.onkeydown=e=>{if($('#view').style.display!='flex')return;const n=B.styles[vs].candidates.length;
 if(e.key=='Escape')$('#view').style.display='none';
 if(e.key=='ArrowRight')open_(vs,vk+1>=n?-1:vk+1);if(e.key=='ArrowLeft')open_(vs,vk-1<-1?n-1:vk-1);
 if((e.key=='b'||e.key=='B')&&vk>=0){mark(vs,vk,'best');open_(vs,vk);}if((e.key=='x'||e.key=='X')&&vk>=0){mark(vs,vk,'wrong');open_(vs,vk);}
 if(e.key=='o'||e.key=='O'){$('#vimg').src=`/styles-img/${B.styles[vs].id}/${B.styles[vs].original}`;}};
document.onkeyup=e=>{if((e.key=='o'||e.key=='O')&&$('#view').style.display=='flex')open_(vs,vk);};
$('#rev').onchange=()=>{document.body.classList.toggle('reveal',$('#rev').checked);};
load();
</script></body></html>"""


def _styles_mod():
    """arstro.color.grading.style's catalogue, for its verdict file — the sub-skill sits beside this one."""
    sys.path.insert(0, str(HERE.parent.parent / "arstro.color.grading.style" / "scripts"))
    import styles
    return styles


def serve(args):
    lab = Lab(args)
    ST = _styles_mod()

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

        def exam_dir(self):
            # `?set=<name>`: another graded set under the lab dir (a folder or a symlink to one, holding exam.json) —
            # a real shoot reviewed on the same page as the exam (2026-11-02, ~/photo/myphoto). Default: the exam.
            q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query).get("set", [""])[0]
            return lab.dir / (q if re.fullmatch(r"[A-Za-z0-9_-]{1,40}", q or "") else "exam")

        def do_GET(self):
            u = urllib.parse.urlparse(self.path).path
            if u == "/":
                return self.send(200, PAGE, "text/html; charset=utf-8")
            if u == "/api/round":
                r = lab.round(lab.state["current"])
                r["photos"] = lab.photos
                return self.send(200, json.dumps(r), "application/json")
            if u == "/exam":
                return self.send(200, EXAM_PAGE, "text/html; charset=utf-8")
            if u == "/api/exam":
                f = self.exam_dir() / "exam.json"
                return self.send(200, f.read_text() if f.exists() else '{"photos": []}', "application/json")
            if u.startswith("/exam-img/"):
                rel = urllib.parse.unquote(u[len("/exam-img/"):])
                p = (self.exam_dir() / rel).resolve()
                if p.is_file() and self.exam_dir().resolve() in p.parents:
                    return self.send(200, p.read_bytes(), "image/jpeg")
            if u == "/styles":
                return self.send(200, STYLES_PAGE, "text/html; charset=utf-8")
            if u == "/api/styles":
                f = lab.dir / "styles" / "board.json"
                b = json.loads(f.read_text()) if f.exists() else {"styles": []}
                ver = ST.verdicts()
                for s in b["styles"]:
                    if s["id"] in ver:
                        s["verdict"] = ver[s["id"]]
                    for c in s["candidates"]:
                        c.pop("params", None)
                        c.pop("recipe", None)
                return self.send(200, json.dumps(b, ensure_ascii=False), "application/json")
            if u.startswith("/styles-img/"):
                rel = urllib.parse.unquote(u[len("/styles-img/"):])
                root = (lab.dir / "styles").resolve()
                p = (root / rel).resolve()
                if p.is_file() and root in p.parents:
                    return self.send(200, p.read_bytes(), "image/jpeg")
            if u == "/taste":
                return self.send(200, "<pre style='font:14px/1.5 monospace;padding:16px;white-space:pre-wrap'>%s</pre>"
                                 % html.escape(summary_text()), "text/html; charset=utf-8")
            if u.startswith("/img/"):
                parts = urllib.parse.unquote(u).split("/")
                p = (lab.dir / "rounds" / parts[2] / parts[3]).resolve()
                if p.is_file() and lab.dir.resolve() in p.parents:
                    return self.send(200, p.read_bytes(), "image/jpeg")
            self.send(404, "not found", "text/plain")

        def do_POST(self):
            if self.path == "/api/new":
                # Start a round without a verdict: {"kind": "taste"|"palette", "photo": name}. The agent
                # calls this after learning from the verdicts; "taste" also becomes the session's mode.
                n = int(self.headers.get("content-length", 0))
                body = json.loads(self.rfile.read(n) or b"{}")
                with lab.lock:
                    lab.state["mode"] = body.get("kind", "palette")
                    r = lab.new_round(body.get("photo") or lab.photos[0], body.get("kind", "palette"), None, 1.0)
                return self.send(200, json.dumps({"round": r["id"], "items": len(r["items"])}), "application/json")
            if urllib.parse.urlparse(self.path).path == "/api/exam-judge":
                n = int(self.headers.get("content-length", 0))
                b = json.loads(self.rfile.read(n) or b"{}")
                with lab.lock:
                    f = self.exam_dir() / "exam.json"
                    ex = json.loads(f.read_text())
                    for p in ex["photos"]:
                        if p["name"] == b["name"]:
                            p["verdict"] = {"good": bool(b["good"]), "note": b.get("note", ""),
                                            "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
                            rec = {"at": p["verdict"]["at"], "photo": p["name"], "kind": "exam", "set": self.exam_dir().name,
                                   "good": p["verdict"]["good"],
                                   "note": p["verdict"]["note"], "exam_built": ex.get("built"), "steps": p["steps"]}
                            with open(JUDGMENTS, "a") as fh:
                                fh.write(json.dumps(rec) + "\n")
                    f.write_text(json.dumps(ex, indent=1))
                return self.send(200, '{"saved": true}', "application/json")
            if self.path == "/api/style-judge":
                # A style verdict: which of the three readings IS the word. Saved the moment it is clicked, into the
                # style skill's verdict file (what `styles.recipe()` reads) and the judgments log.
                n = int(self.headers.get("content-length", 0))
                b = json.loads(self.rfile.read(n) or b"{}")
                if b.get("style") not in ST.STYLES:
                    return self.send(400, '{"error": "unknown style"}', "application/json")
                with lab.lock:
                    ver = ST.verdicts()
                    prev = ver.get(b["style"]) or {}
                    board = json.loads((lab.dir / "styles" / "board.json").read_text())
                    s = next((x for x in board["styles"] if x["id"] == b["style"]), None)
                    v = {"best": b.get("best") if b.get("best") in ("A", "B", "C") else None,
                         "wrong": [x for x in b.get("wrong", []) if x in ("A", "B", "C")],
                         "none": bool(b.get("none")), "note": b.get("note", ""),
                         "photo": s and s["photo"], "board": board.get("built"),
                         "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
                    ver[b["style"]] = v
                    ST.VERDICTS.parent.mkdir(parents=True, exist_ok=True)
                    ST.VERDICTS.write_text(json.dumps(ver, ensure_ascii=False, indent=1))
                    by = {c["letter"]: c for c in (s or {}).get("candidates", [])}
                    rec = {"at": v["at"], "kind": "style", "style": b["style"], "photo": v["photo"],
                           "best": v["best"] and {"letter": v["best"], **by.get(v["best"], {}).get("recipe", {})},
                           "wrong": [{"letter": x, **by.get(x, {}).get("recipe", {})} for x in v["wrong"]],
                           "none": v["none"], "note": v["note"]}
                    # the log keeps each DECISION (a pick or "none", or a changed note) — not every click on the way
                    if (v["best"] or v["none"]) and any(prev.get(k) != v[k] for k in ("best", "wrong", "none", "note")):
                        with open(JUDGMENTS, "a") as fh:
                            fh.write(json.dumps(rec, ensure_ascii=False) + "\n")
                return self.send(200, '{"saved": true}', "application/json")
            if self.path not in ("/api/judge", "/api/mark"):
                return self.send(404, "not found", "text/plain")
            if self.path == "/api/judge":
                n = int(self.headers.get("content-length", 0))
                body = json.loads(self.rfile.read(n) or b"{}")
            else:
                body = {}
            if self.path == "/api/mark":
                n = int(self.headers.get("content-length", 0))
                lab.mark(json.loads(self.rfile.read(n) or b"{}"))
                return self.send(200, '{"saved": true}', "application/json")
            if body.get("action") == "photo" and not body.get("best"):
                with lab.lock:
                    lab.new_round(body["photo"], "taste" if lab.state.get("mode") == "taste" else "palette", None, 1.0)
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
