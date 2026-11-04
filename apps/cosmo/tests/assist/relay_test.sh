#!/bin/sh
# R-ASSIST-3 — an agent's MCP tools relayed to a RUNNING cosmo. `cosmo-cc serve --control` stands in
# for the window (same control socket, no display); `cosmo-cc mcp --attach` is what the agent runs.
# Asserted: the five tools are offered; a script edits the served project; a preview comes back as a
# JPEG; a refused line is an error naming why; and the edit is in the SERVED model, not a copy.
# Usage: relay_test.sh <path to cosmo-cc>
set -eu
CC="$1"
W=$(mktemp -d)
SOCK="${XDG_RUNTIME_DIR:-/tmp}/cosmo-relay-$$.sock"
export XDG_CONFIG_HOME="$W/cfg"
SERVE=""
cleanup() { [ -n "$SERVE" ] && kill "$SERVE" 2>/dev/null; rm -rf "$W" "$SOCK"; }
trap cleanup EXIT

# A 64x48 photo, and a project holding it.
{ printf 'P6\n64 48\n255\n'; head -c 9216 /dev/urandom; } > "$W/a.ppm"
printf 'project new %s\nimport %s\nwait load.finished\nproject save\n' "$W/t.cmp" "$W/a.ppm" | "$CC" run - > /dev/null

"$CC" serve --control "$SOCK" --project "$W/t.cmp" 2> "$W/serve.log" &
SERVE=$!
i=0; while [ ! -S "$SOCK" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
[ -S "$SOCK" ] || { echo "FAIL: serve did not open $SOCK"; cat "$W/serve.log"; exit 1; }

python3 - "$CC" "$SOCK" <<'PY'
import json, subprocess, sys
cc, sock = sys.argv[1], sys.argv[2]
p = subprocess.Popen([cc, "mcp", "--attach", sock], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
n = 0
def req(method, params=None):
    global n
    n += 1
    p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": n, "method": method, "params": params or {}}) + "\n")
    p.stdin.flush()
    line = p.stdout.readline()
    assert line, "the relay closed"
    return json.loads(line)["result"]
def call(name, **args):
    r = req("tools/call", {"name": name, "arguments": args})
    return r.get("isError", False), r["content"]
def check(ok, what):
    print("  [%s] %s" % ("ok" if ok else "FAIL", what))
    if not ok: sys.exit(1)

req("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "relay_test", "version": "1"}})
p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n"); p.stdin.flush()
names = sorted(t["name"] for t in req("tools/list")["tools"])
check(names == ["cosmo_analyze", "cosmo_api", "cosmo_preview", "cosmo_run", "cosmo_state"], "the five tools are offered")
err, c = call("cosmo_run", script="wait load.finished\nselect 1\nset exposure=0.5")
check(not err and "params.changed exposure" in c[0]["text"], "a script edits the served project and its events come back")
err, c = call("cosmo_preview", node=1, edge=32)
check(not err and any(x.get("type") == "image" and x.get("mimeType") == "image/jpeg" for x in c), "a preview comes back as a JPEG")
err, c = call("cosmo_run", script="frobnicate")
check(err and "unknown command" in c[0]["text"], "a refused line is an error naming why")
err, c = call("cosmo_analyze", node=1, before=True)
check(not err and "keyEv" in json.loads(c[0]["text"])["analysis"], "analyze answers with the measurement")
p.stdin.close(); p.wait()

# The edit is in the SERVED session: ask it directly, as a terminal would.
out = subprocess.run([cc, "attach", sock], input="state print --params\n", capture_output=True, text=True).stdout
check("exposure=0.5" in out, "the edit landed in the served model, not a copy")
PY
echo "relay_test: all passed"
