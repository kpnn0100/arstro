#!/bin/sh
# R-ASSIST-1..6 — the user's own agent, run by cosmo, editing the open project. No network: a fake
# `claude` on PATH speaks Claude Code's stream-JSON protocol and, like the real one, starts the MCP
# server cosmo configured for it (`cosmo-cc mcp --attach`) and edits through it. `serve --control`
# stands in for the window. Asserted: the agent is started in cosmo's workspace with the bundled skill,
# the MCP config and COSMO_CONTROL; its edit lands in the SERVED project; its words, its tool use and
# the end of its turn are in the conversation; a second message reaches the same process; stop ends
# it; a one-shot agent answers from its stdin; a missing agent is refused with why.
# Usage: agent_test.sh <path to cosmo-cc> <skills dir>
set -eu
CC="$1"
SKILLS="$2"
W=$(mktemp -d)
SOCK="${XDG_RUNTIME_DIR:-/tmp}/cosmo-agent-$$.sock"
export XDG_CONFIG_HOME="$W/cfg"
export COSMO_SKILLS_DIR="$SKILLS"
SERVE=""
cleanup() { [ -n "$SERVE" ] && kill "$SERVE" 2>/dev/null; rm -rf "$W" "$SOCK"; }
trap cleanup EXIT
mkdir -p "$W/bin"

cat > "$W/bin/claude" <<'PY'
#!/usr/bin/env python3
# A stand-in for Claude Code: checks what cosmo gave it, then answers each user message by
# editing through cosmo's MCP server and replying in stream-JSON.
import json, os, subprocess, sys
argv = sys.argv[1:]
cfg = json.load(open(argv[argv.index("--mcp-config") + 1]))["mcpServers"]["cosmo"]
assert cfg["args"][:2] == ["mcp", "--attach"] and cfg["args"][2] == os.environ["COSMO_CONTROL"], cfg
assert "--allowedTools" in argv and "mcp__cosmo" in argv[argv.index("--allowedTools") + 1]
assert os.path.isfile(".claude/skills/arstro.color.grading/SKILL.md"), os.getcwd()
assert "INSIDE Cosmo" in argv[argv.index("--append-system-prompt") + 1]
def out(o): print(json.dumps(o), flush=True)
out({"type": "system", "subtype": "init", "claude_code_version": "0-fake", "model": "fake-model"})
mcp = subprocess.Popen([cfg["command"]] + cfg["args"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
n = 0
def call(method, params):
    global n
    n += 1
    mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": n, "method": method, "params": params}) + "\n"); mcp.stdin.flush()
    return json.loads(mcp.stdout.readline())["result"]
call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "fake", "version": "0"}})
turn = 0
for line in sys.stdin:
    msg = json.loads(line)["message"]["content"]
    turn += 1
    script = "wait load.finished\nselect 1\nset exposure=0.7" if turn == 1 else "set contrast=12"
    out({"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "mcp__cosmo__cosmo_run", "input": {"script": script}}]}})
    r = call("tools/call", {"name": "cosmo_run", "arguments": {"script": script}})
    ok = not r.get("isError")
    out({"type": "assistant", "message": {"content": [{"type": "text", "text": "turn %d: %s -> %s" % (turn, msg, "edited" if ok else "refused")}]}})
    out({"type": "result", "subtype": "success", "is_error": False, "duration_ms": 1200, "total_cost_usd": 0.001, "result": "ok"})
PY
cat > "$W/bin/echo-agent" <<'SH'
#!/bin/sh
printf 'one-shot heard: '; cat; printf 'project=%s\n' "$COSMO_PROJECT"
SH
chmod +x "$W/bin/claude" "$W/bin/echo-agent"

{ printf 'P6\n64 48\n255\n'; head -c 9216 /dev/urandom; } > "$W/a.ppm"
printf 'project new %s\nimport %s\nwait load.finished\nproject save\n' "$W/t.cmp" "$W/a.ppm" | "$CC" run - > /dev/null

PATH="$W/bin:$PATH" "$CC" serve --control "$SOCK" --project "$W/t.cmp" 2> "$W/serve.log" &
SERVE=$!
i=0; while [ ! -S "$SOCK" ] && [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done
[ -S "$SOCK" ] || { echo "FAIL: serve did not open $SOCK"; cat "$W/serve.log"; exit 1; }

check() { if eval "$1"; then echo "  [ok] $2"; else echo "  [FAIL] $2"; echo "---- output"; cat "$W/out"; echo "---- serve"; cat "$W/serve.log"; exit 1; fi; }

printf 'assist send warm it up\nwait assist.state ready\nassist send a bit more contrast\nwait assist.state ready\nstate print --params\n' \
  | "$CC" attach "$SOCK" --timeout 90 > "$W/out" 2>&1 || true
check 'grep -q "assist.state starting agent=Claude Code" "$W/out"' "Claude Code is started (the default agent)"
check 'grep -q "assist.message info Claude Code 0-fake - fake-model" "$W/out"' "it introduces itself"
check 'grep -q "assist.message tool cosmo_run: wait load.finished;select 1;set exposure=0.7" "$W/out"' "its tool use is in the conversation"
check 'grep -q "assist.message agent turn 1: warm it up -> edited" "$W/out"' "its answer is in the conversation"
check 'grep -q "assist.message agent turn 2: a bit more contrast -> edited" "$W/out"' "a second message reaches the same process"
check 'grep -q "assist.message info done in 1 s - \$0.001" "$W/out"' "the end of a turn says what it took"
check 'grep -q "^exposure=0.7" "$W/out" && grep -q "^contrast=12" "$W/out"' "its edits landed in the SERVED project"

printf 'assist stop\nsettings set agent=echo-agent\nassist send hello there\nwait assist.state ready\nsettings set agent=no-such-agent-here\nassist stop\nassist start\n' \
  | "$CC" attach "$SOCK" --timeout 60 > "$W/out" 2>&1 || true
check 'grep -q "assist.state off" "$W/out"' "stop ends it"
check 'grep -q "assist.message agent one-shot heard: hello there" "$W/out"' "a one-shot agent answers from its stdin"
check 'grep -q "project=$W/t.cmp" "$W/out"' "and is told which project is open"
check 'grep -q "no-such-agent-here. was not found on PATH" "$W/out"' "a missing agent is refused with why"
echo "agent_test: all passed"
