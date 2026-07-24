---
name: android.theme.implement
description: Use to build (or resume building, on any computer) the Android-17-style touch shell theme for GNOME + KDE Plasma on Ubuntu/Fedora, built on Arstro Artboard. This is a long, multi-session, multi-milestone project (launcher, status bar, quick settings, notification panel, gesture navigation, fullscreen/split, animations, icons) tracked by a committed progress ledger so work continues seamlessly across machines. Invoke when the user says things like "continue the android theme", "work on the android theme", "what's next on the android shell", "/android.theme.implement", or resumes this project after moving computers. It reads the ledger, does the next task, updates the ledger, and commits — until the theme is complete on BOTH desktops.
---

# android.theme.implement

The **resume-driven** workflow for the Android-theme-for-Linux project. It is designed so you can
stop on one computer, `git pull` on another, invoke this skill, and it knows exactly what to do next
— because all state lives in committed files, never in machine-local memory.

## The three source-of-truth files (all committed, all travel via git)

1. **The plan** — `launcher/docs/android-theme-plan.md`. The full design: every milestone, every
   concrete value (sizes, colors, durations, D-Bus interfaces), the architecture, the risks. **Read
   the relevant milestone's plan section before starting that milestone.**
2. **The progress ledger** — `launcher/docs/android-theme-progress.md`. *What is done and what to do
   next.* Has a **► NEXT** pointer at the top, granular checkboxes per milestone (M0–M9), a decisions
   log, and a verification "honesty ledger". **This is the file you read first and update last, every
   session.**
3. **This skill** — the procedure below.

Everything the project produces (shell code under `launcher/android-shell/`, docs, this skill, the
ledger) lives in the **`arstro` umbrella repo** (remote `kpnn0100/arstro`). The 153GB
`launcher/plasma.android/android17` AOSP reference tree is **gitignored** — it is reference only,
never built, never committed, and you must **never** `grep`/`find`/`du` its root (see plan §0).
Artboard-library changes go to the **Artboard repo** on its `feature/1.0.0` branch (see §Routing).

## The loop (do this every invocation)

1. **Orient.** Read `launcher/docs/android-theme-progress.md` — specifically the **► NEXT** line and
   the current milestone's checklist. Read that milestone's section in
   `launcher/docs/android-theme-plan.md`. If NEXT is unclear or the ledger looks stale vs. the actual
   repo (e.g. a task marked `[ ]` whose code already exists), reconcile the ledger to reality first
   and say so.
2. **Scope one task.** Take the single next unchecked (`[ ]`) task under the current milestone (top to
   bottom). Do **one task per work session** — they are sized for that. Mark it `[~]` in the ledger.
   If the user named a specific task/milestone, do that instead.
3. **Route to the right sub-workflow** (see §Routing) and implement it, following that workflow's rules
   fully. Match surrounding code style. Everything-animates / touch-first / one-accent-color rules from
   the plan and the Artboard skills apply to all UI.
4. **Verify at the task's test level** (the ledger states L0–L4 per milestone; see §Test levels). Never
   mark a task `[x]` on "it compiles" alone — meet its stated DoD. If you did the work but could not
   verify it (no display, no emcc, no VM), mark it `[!]` and add a line to the ledger's **Verification
   notes** saying exactly what is unverified and why.
5. **Update the ledger** (this is not optional — it is how the next session/computer knows where you
   left off):
   - Tick the task `[x]` (or `[!]`), and tick the milestone in the glance table if it just completed.
   - Rewrite the **► NEXT** line to point at the new next task (next unchecked task, or the next
     milestone's first task, or — if everything is done — the §"Whole-project done" review).
   - Update the "Last updated" date and last-commit note.
   - If you made any decision that departs from the plan or resolves an open item, add it to the
     **Decisions & deviations log** (newest first) so it is never re-litigated on another machine.
6. **Commit** (see §Commit). One focused commit per task, in whichever repo(s) it touched, **including
   the ledger update**. Then stop and report what you did and what NEXT now points to — unless the user
   asked you to keep going, in which case loop back to step 1.

Do not skip step 5. A finished task whose ledger was not updated is the one failure mode that breaks
"resume on another computer".

## Routing — which sub-workflow a task uses

- **Task changes the Artboard library** (`Artboard/` — namespace `artboard`, the render/UI/input core,
  any `IRenderTarget` primitive, a new control, a gesture-recognizer change): use the
  **`implement_artboard`** skill (its full V-model: requirements → architecture → detailed_design →
  puml → code → RecordingTarget tests at 100% core coverage → build all adapters → commit to Artboard
  `main`… **but note:** this project commits Artboard work to **`feature/1.0.0`**, the repo's real
  active line — see the ledger's decisions log). M0 is entirely this. Later milestones occasionally
  need a new Artboard primitive — when they do, it is a mini `implement_artboard` cycle, never an
  inline hack in an app or adapter.
- **Task builds/styles the shell app UI** (`launcher/android-shell/` surfaces, widgets, screens,
  panels — the launcher, status bar, QS, notifications, recents, drawn with Artboard segments): follow
  the **`arstro.design.desktop`** skill's design language (tokens, motion, layout, states) but with the
  Android-theme palette/metrics from the plan. This is app code, not library code — it consumes
  Artboard, it does not modify it.
- **Task is host / compositor-bridge / services / packaging** (GTK3+layer-shell host, `CompositorBridge`
  + KWin script / GNOME extension, D-Bus service clients, `notifyd`, sessions, deb/rpm): no dedicated
  skill — follow the plan's §3 (architecture), §3.2 (bridges), §3.3 (services), §6 (testing), §7
  (packaging) directly. Keep the platform seam behind `CompositorBridge`/`SystemServices` interfaces
  with fakes, exactly as the plan specifies, so shared milestones stay desktop-agnostic.

When a task spans two (e.g. "add an Artboard primitive, then use it in a panel"), do the Artboard
mini-cycle first (own commit), then the app work (own commit).

## Test levels (from plan §6 — the project is testable in full isolation from the host desktop)

- **L0** unit — RecordingTarget op-stream + fake services/NullBridge + scripted `RawPointer`. No display.
- **L1** golden images — render surfaces headlessly via `CairoTarget` → PNG, diff vs committed baselines.
- **L2** nested KWin — `dbus-run-session -- env XDG_RUNTIME_DIR=$(mktemp -d) kwin_wayland --width … ./arstro-android-shell` — a window on the dev desktop; private bus + runtime dir, so it never touches the host session.
- **L3** nested GNOME — `dbus-run-session -- env HOME=$(mktemp -d) gnome-shell --nested --wayland` with the extension in the scratch HOME.
- **L4** VMs — Ubuntu/Fedora GNOME + KDE, snapshots, for session + packaging acceptance.

Prefer the lowest level that proves the task; the ledger names the level per milestone. If the needed
level's tooling is absent on this machine (no `kwin_wayland`, no VM, no `emcc`), do the work + the levels
you *can* run, mark the rest `[!]`, and note it — do not block the whole project on one machine's gaps.

## Commit rules

- One focused commit per task, in each repo it touched (umbrella `arstro`, and/or `Artboard` with the
  ledger noting the commit hash). **Always include the ledger update in the umbrella commit.**
- End every commit message with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
  (or the model actually in use).
- **Do not `git push`** unless the user asks — pushing is what actually makes it portable, so remind
  the user to push at the end of a session, but let them do it. Never `git add` the gitignored 153GB
  tree; after a broad `git add`, run `git status` and confirm only intended files are staged.
- Umbrella repo commits to `main` (its established convention — see its recent `cosmo(android)` history).
  Artboard commits to `feature/1.0.0`.

## Definition of done (the whole project)

Not done until the ledger's **§"Whole-project done"** is fully checked: all of M1–M6, **Plasma (M7)**
AND **GNOME (M8)** feature-complete, M9 packaging + VM-matrix smoke checklist green on all four targets,
the M0 web-adapter verification gap cleared, and the 7 user deliverables demonstrably working on **both**
desktops. "Works on Plasma" is half done — the GNOME track (M8) is a first-class requirement, not optional.

## Working style

Work lean and single-threaded by default (the user has stopped agent fleets over token cost — see the
project memory). Do not spin up subagent fleets or multi-agent workflows unless the user explicitly asks.
One task, verified, ledger updated, committed — then report.
