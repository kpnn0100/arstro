/*
 *  cosmo — MarkdownText: an agent's reply drawn as formatted text, not as its markup (R-ASSIST-12).
 *
 *  Every agent the Agent tab can run - Claude Code, Gemini CLI, Codex, OpenCode, Goose, a local
 *  model through `cosmo-cc agent`, a plain program - answers in the one format chat agents share:
 *  GitHub-flavoured Markdown. Drawn verbatim it reads as `**Exposure** lifted by \`+0.3\``; this
 *  turns it into what the agent meant. The service keeps the agent's text exactly as it arrived
 *  (R-SVC-4: how text LOOKS is the view's - the web view will render the same model with HTML).
 *
 *  Two halves, so each is testable on its own:
 *    parse()   text -> blocks of styled spans. Pure: no render target, no Artboard type beyond none.
 *    layout()  blocks -> positioned runs and decorations, wrapped with the target's REAL metrics
 *              (R5). Done once per width and per text change by the caller, never per frame.
 *    paint()   draws a layout at an origin with an opacity - the only per-frame part.
 *
 *  What it understands: **bold** __bold__ *italic* _italic_ ***both*** ~~strike~~ `code`
 *  [links](url) <autolinks> bare https:// URLs, \escapes; # headings, - * + bullets (nested),
 *  1. 1) ordered items, - [ ] task items, > quotes, ``` and ~~~ fenced code, --- rules, and
 *  | pipe | tables |. A single newline is a line break, as in every chat (GitHub comments, not
 *  CommonMark's soft break): agents write one line per point and mean it.
 *
 *  What "every agent" also means, learned from what they actually send:
 *    - a reply STREAMS in pieces (R-ASSIST-9), so a span is often unfinished: an opened `**` or a
 *      backtick with no closer yet styles to the end of its paragraph instead of flashing as
 *      literal stars until the closer arrives; an unclosed ``` fence is code to the end;
 *    - reasoning models (DeepSeek-R1, Qwen3 on Ollama) put their thinking in <think>…</think>:
 *      it is not the answer - an open think shows as one muted "Thinking…" line, a closed one
 *      is dropped;
 *    - a `cmd:` program may colour its output: ANSI escapes are removed; \r\n is \n; a tab is
 *      four spaces;
 *    - emoji: the app's own faces are compiled in (R-FONT-1) and carry none, so a pictograph
 *      would draw as an empty box. A check mark (✓ ✔ ✅ ☑) and a cross (✗ ✘ ❌) are drawn as
 *      vector marks in the success / destructive colour; every other pictograph is dropped;
 *      arrows, box drawing and maths Roboto lacks are drawn in JetBrains Mono, which has them.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
namespace md
{
    enum Style : uint16_t
    {
        Plain = 0,
        Bold = 1,
        Italic = 2,
        Code = 4,
        Link = 8,
        Strike = 16,
        Symbol = 32,   // a glyph Roboto lacks: drawn in the mono face
        Tick = 64,     // a check mark, drawn as a vector mark (no glyph exists)
        Cross = 128,   // a cross mark, likewise
        Break = 256,   // a line break inside a block (the span's text is empty)
    };

    struct Span
    {
        std::string text;
        uint16_t style = Plain;
    };

    enum class BlockKind { Paragraph, Heading, Bullet, Ordered, Task, Quote, CodeBlock, Rule, Table, Thinking };

    struct Block
    {
        BlockKind kind = BlockKind::Paragraph;
        int level = 0;                    // heading 1..6; list nesting depth 0..3
        std::string marker;               // an ordered item's "1." (as written)
        bool done = false;                // a task item's [x]
        std::vector<Span> spans;          // the text of every kind but CodeBlock / Table / Rule
        std::vector<std::string> code;    // CodeBlock: its lines, verbatim
        std::vector<std::vector<std::vector<Span>>> rows;   // Table: row 0 is the header
        std::vector<char> align;          // Table: per column 'l' 'c' 'r'
    };

    /** The whole reply. */
    std::vector<Block> parse(const std::string &text);
    /** One line of inline markup - also what a one-line tool title goes through. */
    std::vector<Span> parseInline(const std::string &text);
    /** The text a reader sees, markers gone - for a line that is drawn as one muted string. */
    std::string plain(const std::string &text);

    // ── layout ──
    enum class Ink : uint8_t { Body, Muted, Quote, Link, Success, Danger };

    struct Run
    {
        std::string text;
        double x = 0, baseline = 0, w = 0, px = 11.0;
        const char *font = nullptr;
        Ink ink = Ink::Body;
        uint16_t style = Plain;   // Code draws a chip behind; Link and Strike a line; Tick/Cross a mark
    };

    struct Deco
    {
        enum class Kind : uint8_t { CodeBox, QuoteBar, Rule, TableLine, Bullet, TaskBox } kind;
        artboard::Rect r;
        bool filled = false;   // TaskBox: done; Bullet: a filled (depth 0) or open dot
    };

    struct Layout
    {
        std::vector<Run> runs;
        std::vector<Deco> decos;
        double height = 0.0;
    };

    struct Metrics
    {
        double px = 11.0;          // body size (the type ramp's 11)
        double lineH = 15.6;       // 11 * 1.42
        double codePx = 10.0;      // mono, a size down: JetBrains Mono's x-height is taller
        double codeLineH = 13.0;   // ladder 4
    };

    Layout layout(const artboard::IRenderTarget &t, const std::vector<Block> &blocks, double width, const Metrics &m = {});

    struct Inks
    {
        artboard::Color body, muted, quote, link, success, danger, chip, codeBox, line;
    };
    /** The cosmo palette's inks, `alpha` applied to all (a message fading in). `body` replaces the
     *  foreground (an error is drawn in the destructive colour). */
    Inks themeInks(double alpha);

    /** Draws `l` with its top-left at (x, y). Runs outside [clipTop, clipBottom] are skipped. */
    void paint(artboard::IRenderTarget &t, const Layout &l, double x, double y, const Inks &inks,
               double clipTop = -1e9, double clipBottom = 1e9);
}
}
}
