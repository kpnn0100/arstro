#!/usr/bin/env python3
"""serve.py — the melody lab on localhost: every melody of a batch to hear, see (a piano roll over its chords, the
theory it uses) and JUDGE. The user is the approver; each verdict is appended to data/judgments.jsonl with the whole
melody, so taste.py can learn from it and the record stands on its own.

    python3 serve.py [--lab ~/Music/Solaris/melody-lab] [--port 8787]

Binds 127.0.0.1 only. Batches are the folders of the lab holding a batch.json (melody.py generate + render.py).
"""
import argparse, datetime, json, os, re
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.dirname(HERE)
DATA = os.path.join(SKILL, "data")
PAGE = os.path.join(SKILL, "assets", "audition.html")
JUDGMENTS = os.path.join(DATA, "judgments.jsonl")
SAFE = re.compile(r"^[A-Za-z0-9._-]+$")
VERDICTS = {1: "no", 2: "weak", 3: "okay", 4: "good", 5: "love"}
KEEP = ("id", "style", "style_label", "bpm", "key", "tonic", "mode", "progression", "chords", "chord_pcs", "chord_roots",
        "rhythm", "form", "motif_steps", "motif_kind", "notes", "devices", "voice", "metrics", "rubric", "score",
        "song", "title", "sections", "lyrics")                 # a song (songbook.py) carries its sections and words


def batches(lab):
    return sorted(d for d in os.listdir(lab) if SAFE.match(d) and os.path.isfile(os.path.join(lab, d, "batch.json")))


def latest_verdicts(batch):
    out = {}
    if os.path.exists(JUDGMENTS):
        with open(JUDGMENTS) as f:
            for row in f:
                try:
                    j = json.loads(row)
                except ValueError:
                    continue
                if j.get("batch") == batch:
                    out[j["id"]] = {"rating": j["rating"], "verdict": j["verdict"], "note": j.get("note", "")}
    return out


def make_handler(lab):
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype="application/json", extra=None):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            if u.path == "/":
                with open(PAGE, "rb") as f:
                    return self.send(200, f.read(), "text/html; charset=utf-8")
            if u.path == "/api/batches":
                return self.send(200, batches(lab))
            if u.path in ("/api/batch", "/api/verdicts"):
                name = q.get("name", [""])[0]
                if not SAFE.match(name) or name not in batches(lab):
                    return self.send(404, {"error": "no such batch"})
                if u.path == "/api/verdicts":
                    return self.send(200, latest_verdicts(name))
                with open(os.path.join(lab, name, "batch.json"), "rb") as f:
                    return self.send(200, f.read())
            m = re.match(r"^/audio/([^/]+)/([^/]+\.mp3)$", u.path)
            if m and SAFE.match(m.group(1)) and SAFE.match(m.group(2)):
                path = os.path.join(lab, m.group(1), m.group(2))
                if not os.path.isfile(path):
                    return self.send(404, {"error": "no such file"})
                size = os.path.getsize(path)
                rng = self.headers.get("Range")
                with open(path, "rb") as f:
                    if rng and rng.startswith("bytes="):              # seeking: the audio element asks for a range
                        a, _, b = rng[6:].partition("-")
                        start = int(a) if a else 0
                        end = min(int(b) if b else size - 1, size - 1)
                        f.seek(start)
                        chunk = f.read(end - start + 1)
                        return self.send(206, chunk, "audio/mpeg", {"Content-Range": f"bytes {start}-{end}/{size}",
                                                                     "Accept-Ranges": "bytes"})
                    return self.send(200, f.read(), "audio/mpeg", {"Accept-Ranges": "bytes"})
            return self.send(404, {"error": "not found"})

        def do_POST(self):
            if urlparse(self.path).path != "/api/verdict":
                return self.send(404, {"error": "not found"})
            try:
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
                batch, mid, rating = body["batch"], body["id"], int(body["rating"])
                note = str(body.get("note", ""))[:2000]
            except (ValueError, KeyError, TypeError):
                return self.send(400, {"error": "batch, id and a rating 1–5 are needed"})
            if rating not in VERDICTS or not SAFE.match(batch) or batch not in batches(lab):
                return self.send(400, {"error": "a rating is 1–5, in a batch that exists"})
            with open(os.path.join(lab, batch, "batch.json")) as f:
                mel = next((m for m in json.load(f)["melodies"] if m["id"] == mid), None)
            if not mel:
                return self.send(404, {"error": "no such melody"})
            row = {"at": datetime.datetime.now().isoformat(timespec="seconds"), "batch": batch, "id": mid,
                   "rating": rating, "verdict": VERDICTS[rating], "note": note,
                   "melody": {k: mel[k] for k in KEEP if k in mel}}
            os.makedirs(DATA, exist_ok=True)
            with open(JUDGMENTS, "a") as f:
                f.write(json.dumps(row, ensure_ascii=False) + "\n")
            return self.send(200, {"saved": True, "verdict": VERDICTS[rating]})

    return H


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lab", default=os.path.expanduser("~/Music/Solaris/melody-lab"))
    ap.add_argument("--port", type=int, default=8787)
    a = ap.parse_args()
    lab = os.path.abspath(a.lab)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(lab))
    print(f"melody lab: http://127.0.0.1:{a.port}/  (batches in {lab}; verdicts to {JUDGMENTS})", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
