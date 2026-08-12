# Genesis

An animation designer for Arstro **Artboard** whose deliverable is source code.

Pick an abstract control to inherit from (`VisualLoop`, `ProgressIndicator`, `Button`,
`Slider`, `Checkbox`), draw the component, bind its geometry to expressions, wire
animations to the base class's events — Genesis writes a `.h` / `.cpp` pair that compiles
against `artboard::` and nothing else. No Genesis runtime ships with your app.

**Status: proposal, nothing implemented.**

- [docs/brief.md](docs/brief.md) — the product brief (mental model, language, codegen,
  the Artboard changes it needs, milestones, open decisions).

Artboard-side work (`Segment::opacity`, `Segment::rotation`, the `VisualLoop` base, …) is
tracked as proposed FR-32…FR-37 in the brief §9 and lands via `/implement_artboard` in the
`Artboard/` repo.
