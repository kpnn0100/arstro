# core/Mcp — an Arstro app as a Model Context Protocol server

**MCP** (modelcontextprotocol.io) is how an agent's client — Claude Code and others — discovers and
calls tools: JSON-RPC 2.0, one message per line on stdio. This library is that protocol and nothing
else. Every Arstro app already has ONE way in (a text command its service parses) and ONE way out
(its model and its events), so an MCP tool is one more **front end** over exactly those, a peer of
the window, the CLI, the control socket and the NTWB bridge (arstro.rule §1). It holds no app
behaviour.

| file | what |
|---|---|
| `include/mcp/Server.h` · `src/Server.cpp` | `Server`: `initialize` / `ping` / `tools/list` / `tools/call`, argument validation, image content, the JSON-RPC errors. `handle(line)` is pure. |
| `src/Stdio.cpp` | **the only OS seam**: `serveStdio` keeps the real stdout for the protocol and points fd 1 at stderr |
| `include/mcp/ServiceTools.h` · `src/ServiceTools.cpp` | `addTextServiceTools`: `<app>_api` / `<app>_run` / `<app>_state` for any service whose way in is a text line |
| `serve.sh` | what `.mcp.json` runs: `core/Mcp/serve.sh cosmo|interstellar|solaris` |
| `tests/mcpTests.cpp` | `mcp_tests` (ctest) |

Uses `core/Ntwb`'s dependency-free `Json` rather than a second JSON type.

## Who serves what

| app | `… mcp` | tools |
|---|---|---|
| cosmo | `cosmo-cc mcp` | `cosmo_api` (grammar + every `set` key in engine units), `cosmo_run`, `cosmo_state`, `cosmo_preview` (a JPEG of the edit or as shot), `cosmo_analyze` (`analyze` + metadata) |
| interstellar | `interstellar-cc mcp` | `interstellar_api`, `interstellar_run`, `interstellar_state`, `interstellar_frame` (a graded frame, PNG) |
| solaris | `solaris-cc mcp` | `solaris_api`, `solaris_run`, `solaris_state` — one song session, undo kept |

Build the three CLIs, and a Claude Code session started in the repo root offers them (`.mcp.json`;
approve the project servers once). The color grading skill (`.claude/skills/arstro.color.grading`)
is the worked example of an agent using `cosmo-cc mcp`.

## Requirements (as built)

- **MR-1 Protocol only.** No tool is defined here except the generic text-service three; an app
  supplies name, schema and a function that goes through its own grammar.
- **MR-2 An undeclared argument is refused, naming it and the accepted ones** (`Server::validate`);
  a property's declared `type` (string / integer / number / boolean / array / object) is checked and
  `required` ones must be present (arstro.rule §5).
- **MR-3 A tool's failure is a result, not a protocol error**: a refused line, a bad argument or a
  thrown exception comes back with `isError: true` and the reason. Protocol errors are for the
  protocol: -32700 parse, -32600 not a request (MCP has no batches), -32601 unknown method,
  -32602 unknown tool.
- **MR-4 stdout is the protocol's** (`serveStdio`); everything else an app prints goes to stderr.
- **MR-5 Revisions** 2025-06-18, 2025-03-26 and 2024-11-05; the client's is echoed when known.
- **MR-6 `<app>_run` runs lines in order and stops at the first refusal**, naming the line, and
  returns only the events this call caused.
