# core/Ntwb — the app side of NTWB for Arstro apps

**NTWB (native-to-web bridge) 1.1.0** lets an Arstro app keep its core on the device and show its
UI in a browser through **Arstro Remote**, which lists the app under *Apps*, launches it, serves its
web UI and relays messages and binary streams. The protocol is defined once, in Arstro Remote
(`server/arstro_remote/ntwb/spec.py`, rendered to `docs/ntwb/API.md` + `api.json`, explained in
`docs/ntwb/NTWB.md`). This library is its C++ app side; cosmo (`cosmo-cc ntwb`) is the worked example.

| file | what |
|---|---|
| `include/ntwb/Json.h` | a small, dependency-free JSON value (ordered objects, exact integers, locale-free numbers) |
| `include/ntwb/Protocol.h` | NTWB as tables (messages, directions, fields, blob header, environment), `validate()`, framing (`encodeJson`, `encodeBlob`, `Decoder`) |
| `include/ntwb/Transport.h` | **the only platform seam**: `ITransport`, `UnixSocketTransport` (POSIX), `MemoryTransport` (tests) |
| `include/ntwb/Client.h` | the app: `connect()` (NTWB_SOCKET / NTWB_TOKEN), `poll()`, handlers for call / notify / client open-close / bye, and `reply`, `event`, `state`, `blob`, `log`, `bye` |
| `spec/api.json` | **vendored** copy of Arstro Remote's generated catalogue |
| `tests/ntwbTests.cpp` | `ntwb_tests` (ctest): JSON, framing byte by byte, validation, the client over a memory transport, and **the table against `spec/api.json`** |

## Requirements (as built)

- **NR-1** Platform-free core, one seam: everything but `Transport.cpp` is standard C++17 with no
  OS calls, so the library builds wherever an Arstro core builds; the client never starts a thread
  (`poll()` runs on the caller's clock, like `CosmoService::pump`).
- **NR-2** One protocol, provably: `Protocol.cpp`'s table equals the vendored `spec/api.json` in
  every message, direction, field, type and required flag, and in the blob header and the
  environment list (`table_matches_the_vendored_spec`, which fails on any drift). Arstro Remote
  owns the spec; this repo follows it.
- **NR-3** Nothing invalid leaves: `Client` validates every message and blob header it sends and
  reports a refusal in `lastError()` instead of sending (NTWB-06).
- **NR-4** A refused handshake, a host `bye`, a closed socket or a broken stream ends `poll()` with
  the reason in `lastError()`.
- **NR-5** Sessions need no app code (NTWB 1.1): each session of an app is its own process, launched
  with `NTWB_SESSION`; `session()` returns the id the host sent in `welcome` (`main`, `s2` ...).
  The app is the MODEL of its session (NTWB-11) - publish state, never a picture of a window.

## Changing the protocol

1. Change `spec.py` in Arstro Remote, regenerate (`python3 -m arstro_remote.ntwb.spec --write docs/ntwb`),
   commit there (its `test_ntwb.py` checks the docs, `ntwb.js` and the narrative).
2. Copy the new `docs/ntwb/api.json` over `spec/api.json` here - `ntwb_tests` now fails.
3. Change `Protocol.cpp` (and `Client` if a message gained a field) until it passes.

See the `arstro.ntwb.implement` skill.
