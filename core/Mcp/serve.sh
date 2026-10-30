#!/bin/sh
# Start an Arstro app as an MCP server on stdio: `core/Mcp/serve.sh cosmo|interstellar|solaris`.
# What `.mcp.json` runs, so the binary is found from the repo root whatever the client's cwd is.
# Build first: cmake --build build --target cosmo-cc interstellar-cc solaris-cc
app="$1"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
bin="${ARSTRO_BUILD:-$root/build}/apps/$app/cli/$app-cc"
case "$app" in cosmo|interstellar|solaris) ;; *) echo "serve.sh: unknown app '$app' (cosmo | interstellar | solaris)" >&2; exit 2 ;; esac
if [ ! -x "$bin" ]; then
  echo "serve.sh: $bin is not built — cmake --build build --target $app-cc" >&2
  exit 1
fi
exec "$bin" mcp
