#!/bin/sh
# R-ASSIST-9/10 - any agent: an ACP agent, and a local model through cosmo's own agent. No network and no
# model: a fake ACP agent on PATH, and a fake OpenAI-compatible server for `cosmo-cc agent`.
# Asserted, ACP: cosmo initializes, opens a session in its workspace with cosmo's MCP server in it, the
# workspace's AGENTS.md points at the skill, a safe permission is granted and a dangerous one refused (and
# said so), streamed chunks become ONE message, the agent's edit lands in the SERVED project.
# Local: the model's tool call (cosmo_run) edits the served project, its words are the answer.
# Usage: acp_test.sh <path to cosmo-cc> <skills dir>
set -eu
CC="$1"
SKILLS="$2"
W=$(mktemp -d)
SOCK="${XDG_RUNTIME_DIR:-/tmp}/cosmo-acp-$$.sock"
export XDG_CONFIG_HOME="$W/cfg"
export COSMO_SKILLS_DIR="$SKILLS"
SERVE=""; LLM=""
cleanup() { [ -n "$SERVE" ] && kill "$SERVE" 2>/dev/null; [ -n "$LLM" ] && kill "$LLM" 2>/dev/null; rm -rf "$W" "$SOCK"; }
trap cleanup EXIT
mkdir -p "$W/bin"

cat > "$W/bin/fake-acp" <<'PY'
#!/usr/bin/env python3
# An ACP agent: checks what cosmo handed it, asks two permissions, streams, edits through cosmo's MCP server.
import json, os, subprocess, sys
def send(o): print(json.dumps(o), flush=True)
def read():
    l = sys.stdin.readline()
    return json.loads(l) if l else None
pending = {}
def ask(i, method, params):
    send({"jsonrpc": "2.0", "id": i, "method": method, "params": params})
    while True:
        m = read()
        if m.get("id") == i and "method" not in m: return m
mcp = None; session = None
while True:
    m = read()
    if m is None: break
    meth = m.get("method"); p = m.get("params", {})
    if meth == "initialize":
        assert p["protocolVersion"] == 1 and p["clientInfo"]["name"] == "cosmo", p
        send({"jsonrpc": "2.0", "id": m["id"], "result": {"protocolVersion": 1, "agentCapabilities": {}, "agentInfo": {"name": "fake", "title": "Fake ACP", "version": "0"}, "authMethods": []}})
    elif meth == "session/new":
        srv = [s for s in p["mcpServers"] if s["name"] == "cosmo"][0]
        assert srv["args"][:2] == ["mcp", "--attach"] and srv["env"] == [] and os.path.isabs(srv["command"]), srv
        md = open(os.path.join(p["cwd"], "AGENTS.md")).read()
        assert "skills/arstro.color.grading/SKILL.md" in md and os.path.isfile(os.path.join(p["cwd"], "skills/arstro.color.grading/SKILL.md")), md
        assert os.path.isfile(os.path.join(p["cwd"], "GEMINI.md"))
        mcp = subprocess.Popen([srv["command"]] + srv["args"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        def call(method, params, n=[0]):
            n[0] += 1
            mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": n[0], "method": method, "params": params}) + "\n"); mcp.stdin.flush()
            return json.loads(mcp.stdout.readline())["result"]
        call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "fake", "version": "0"}})
        session = "s1"
        send({"jsonrpc": "2.0", "id": m["id"], "result": {"sessionId": session}})
    elif meth == "session/prompt":
        text = "".join(b["text"] for b in p["prompt"] if b["type"] == "text")
        assert "[Context from Cosmo" in text and "skills/arstro.color.grading/SKILL.md" in text, text[:300]
        opts = [{"optionId": "yes", "name": "Allow", "kind": "allow_once"}, {"optionId": "no", "name": "Reject", "kind": "reject_once"}]
        def upd(u): send({"jsonrpc": "2.0", "method": "session/update", "params": {"sessionId": session, "update": u}})
        upd({"sessionUpdate": "tool_call", "toolCallId": "t1", "title": "python3 edit.py review", "kind": "execute", "status": "pending"})
        r1 = ask(100, "session/request_permission", {"sessionId": session, "toolCall": {"toolCallId": "t1", "kind": "execute", "rawInput": {"command": "python3 edit.py review x.cmp"}}, "options": opts})
        upd({"sessionUpdate": "tool_call", "toolCallId": "t2", "title": "rm -rf ~/photo", "kind": "execute", "status": "pending"})
        r2 = ask(101, "session/request_permission", {"sessionId": session, "toolCall": {"toolCallId": "t2", "kind": "execute", "rawInput": {"command": "rm -rf ~/photo"}}, "options": opts})
        allowed = r1["result"]["outcome"] == {"outcome": "selected", "optionId": "yes"}
        refused = r2["result"]["outcome"] == {"outcome": "selected", "optionId": "no"}
        upd({"sessionUpdate": "tool_call_update", "toolCallId": "t2", "status": "failed"})
        r = call("tools/call", {"name": "cosmo_run", "arguments": {"script": "wait load.finished\nselect 1\nset exposure=0.6"}})
        edited = not r.get("isError")
        for chunk in ["Warm ", "light, ", "permissions %s/%s, edited %s." % ("ok" if allowed else "BAD", "ok" if refused else "BAD", edited)]:
            upd({"sessionUpdate": "agent_message_chunk", "content": {"type": "text", "text": chunk}})
        send({"jsonrpc": "2.0", "id": m["id"], "result": {"stopReason": "end_turn"}})
PY
chmod +x "$W/bin/fake-acp"

cat > "$W/llm.py" <<'PY'
# A fake OpenAI-compatible endpoint: the first turn asks for a cosmo_run edit, the second answers.
import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert self.path == "/v1/chat/completions" and body["model"] == "fake-model", (self.path, body.get("model"))
        names = [t["function"]["name"] for t in body["tools"]]
        assert "cosmo_grade" in names and "cosmo_run" in names, names
        if body["messages"][-1]["role"] == "tool":
            msg = {"role": "assistant", "content": "Brightened photo 1 a little (tool said: %s)." % body["messages"][-1]["content"][:40].replace("\n", " ")}
        else:
            msg = {"role": "assistant", "content": None, "tool_calls": [{"id": "c1", "type": "function", "function": {"name": "cosmo_run", "arguments": json.dumps({"script": "wait load.finished\nselect 1\nset exposure=0.4"})}}]}
        out = json.dumps({"choices": [{"message": msg, "finish_reason": "stop"}]}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)
s = HTTPServer(("127.0.0.1", 0), H)
print(s.server_address[1], flush=True)
s.serve_forever()
PY

{ printf 'P6\n64 48\n255\n'; head -c 9216 /dev/urandom; } > "$W/a.ppm"
printf 'project new %s\nimport %s\nwait load.finished\nproject save\n' "$W/t.cmp" "$W/a.ppm" | "$CC" run - > /dev/null
PATH="$W/bin:$PATH" "$CC" serve --control "$SOCK" --project "$W/t.cmp" 2> "$W/serve.log" &
SERVE=$!
i=0; while [ ! -S "$SOCK" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
check() { if eval "$1"; then echo "  [ok] $2"; else echo "  [FAIL] $2"; echo "---- output"; cat "$W/out"; echo "---- serve"; cat "$W/serve.log"; exit 1; fi; }

printf 'settings set agent=acp:fake-acp\nassist send warm it up\nwait assist.state ready\nstate print --params\nassist stop\n' \
  | "$CC" attach "$SOCK" --timeout 60 > "$W/out" 2>&1 || true
check 'grep -q "assist.message info Fake ACP - connected over ACP" "$W/out"' "an ACP agent is initialized and given a session"
check 'grep -q "assist.message tool python3 edit.py review" "$W/out"' "its tool calls are in the conversation"
check 'grep -q "assist.message error refused: rm -rf ~/photo" "$W/out"' "a dangerous request is refused, and the user is told"
check 'grep -q "assist.message agent Warm light, permissions ok/ok, edited True." "$W/out"' "streamed chunks are ONE message; the safe permission was granted"
check 'grep -q "^exposure=0.6" "$W/out"' "its edit landed in the SERVED project"

python3 "$W/llm.py" > "$W/port" &
LLM=$!
i=0; while [ ! -s "$W/port" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
PORT=$(cat "$W/port")
printf 'settings set agent="local:http://127.0.0.1:%s/v1|fake-model"\nassist send brighten photo 1\nwait assist.state ready\nstate print --params\n' "$PORT" \
  | "$CC" attach "$SOCK" --timeout 60 > "$W/out" 2>&1 || true
check 'grep -q "assist.message info Local model (fake-model) - connected over ACP" "$W/out"' "a local model connects through cosmo-cc agent"
check 'grep -q "assist.message tool cosmo_run" "$W/out"' "the model's tool call is in the conversation"
check 'grep -q "assist.message agent Brightened photo 1 a little" "$W/out"' "its words are the answer"
check 'grep -q "^exposure=0.4" "$W/out"' "its edit landed in the SERVED project"
echo "acp_test: all passed"
