#include "MarkdownText.h"
#include "../Theme.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
namespace md
{
    using namespace artboard;

    namespace
    {
        // ── text clean-up: what an agent sends that is not text ──

        /** \r\n -> \n, ANSI escapes removed (a `cmd:` program's colours), a tab -> four spaces. */
        std::string normalise(const std::string &in)
        {
            std::string s;
            s.reserve(in.size());
            for (size_t i = 0; i < in.size(); ++i)
            {
                const char c = in[i];
                if (c == '\r') continue;
                if (c == '\t') { s += "    "; continue; }
                if (c == '\x1b')
                {
                    if (i + 1 < in.size() && in[i + 1] == '[')   // CSI: parameters, then one final byte @..~
                    {
                        size_t j = i + 2;
                        while (j < in.size() && !((unsigned char)in[j] >= 0x40 && (unsigned char)in[j] <= 0x7E)) ++j;
                        i = j;
                    }
                    else if (i + 1 < in.size() && in[i + 1] == ']')   // OSC: up to BEL or ESC backslash
                    {
                        size_t j = i + 2;
                        while (j < in.size() && in[j] != '\x07' && !(in[j] == '\x1b' && j + 1 < in.size() && in[j + 1] == '\\')) ++j;
                        i = (j < in.size() && in[j] == '\x1b') ? j + 1 : j;
                    }
                    else
                        ++i;   // a two-byte escape
                    continue;
                }
                s += c;
            }
            return s;
        }

        uint32_t decodeAt(const std::string &s, size_t i, size_t &len)
        {
            const unsigned char c = (unsigned char)s[i];
            len = 1;
            if (c < 0x80) return c;
            const size_t n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
            if (n == 0 || i + n >= s.size()) return c;   // a stray or cut-off byte: itself
            uint32_t cp = n == 3 ? (c & 0x07) : n == 2 ? (c & 0x0F) : (c & 0x1F);
            for (size_t k = 1; k <= n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
            len = n + 1;
            return cp;
        }

        enum class Glyph { Text, Symbol, Tick, Cross, Drop };

        /** Where a codepoint goes: the app's faces are compiled in (R-FONT-1) and carry no emoji. */
        Glyph classify(uint32_t cp)
        {
            if (cp < 0x2000) return Glyph::Text;
            if (cp == 0x2713 || cp == 0x2714 || cp == 0x2705 || cp == 0x2611 || cp == 0x1F5F8) return Glyph::Tick;
            if (cp == 0x2717 || cp == 0x2718 || cp == 0x274C || cp == 0x274E || cp == 0x2716 || cp == 0x2715) return Glyph::Cross;
            if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0x20E3 || (cp >= 0xE0000 && cp <= 0xE007F)) return Glyph::Drop;
            if (cp >= 0x1F000 && cp <= 0x1FAFF) return Glyph::Drop;   // emoji, flags, pictographs
            if (cp >= 0x2600 && cp <= 0x27BF) return Glyph::Drop;     // misc symbols, dingbats
            if (cp >= 0x2B00 && cp <= 0x2BFF) return Glyph::Drop;     // stars, heavy arrows
            if (cp >= 0x2300 && cp <= 0x23FF) return Glyph::Drop;     // hourglass, keyboard symbols
            if (cp >= 0x2190 && cp <= 0x21FF) return Glyph::Symbol;   // arrows: in the mono face only
            if (cp >= 0x2200 && cp <= 0x22FF && cp != 0x2212 && cp != 0x2248 && cp != 0x2260 && cp != 0x2264 && cp != 0x2265)
                return Glyph::Symbol;
            if (cp >= 0x2500 && cp <= 0x25FF && cp != 0x25A0 && cp != 0x25CF && cp != 0x25CB) return Glyph::Symbol;
            return Glyph::Text;
        }

        /** An arrow the mono face lacks, as one it has. */
        std::string symbolText(uint32_t cp, const std::string &raw)
        {
            switch (cp)
            {
            case 0x21D2: case 0x27F9: return "\xE2\x86\x92";   // ⇒ -> →
            case 0x21D0: case 0x27F8: return "\xE2\x86\x90";   // ⇐ -> ←
            case 0x21D4: return "\xE2\x86\x94";                // ⇔ -> ↔
            default: return raw;
            }
        }

        bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\t'; }
        bool isPunct(char c) { return (unsigned char)c < 0x80 && std::ispunct((unsigned char)c); }

        // ── inline ──

        struct Tok
        {
            std::string text;
            uint16_t style = Plain;
            bool delim = false;
            char ch = 0;
            int count = 0, orig = 0;
            bool canOpen = false, canClose = false;
        };

        void addText(std::vector<Tok> &toks, const std::string &text, uint16_t style = Plain)
        {
            if (text.empty()) return;
            if (!toks.empty() && !toks.back().delim && toks.back().style == style && !(style & (Tick | Cross | Break | Symbol)))
                toks.back().text += text;
            else
            {
                Tok t;
                t.text = text;
                t.style = style;
                toks.push_back(t);
            }
        }

        /** Plain text (no markup left in it), split where a glyph needs its own treatment. */
        void addGlyphs(std::vector<Tok> &toks, const std::string &s, uint16_t style)
        {
            std::string run;
            for (size_t i = 0; i < s.size();)
            {
                size_t len = 1;
                const uint32_t cp = decodeAt(s, i, len);
                const std::string raw = s.substr(i, len);
                i += len;
                switch (classify(cp))
                {
                case Glyph::Text: run += raw; break;
                case Glyph::Drop: break;
                case Glyph::Symbol:
                    addText(toks, run, style), run.clear();
                    addText(toks, symbolText(cp, raw), style | Symbol);
                    break;
                case Glyph::Tick:
                case Glyph::Cross:
                    addText(toks, run, style), run.clear();
                    addText(toks, " ", style | (classify(cp) == Glyph::Tick ? Tick : Cross));
                    break;
                }
            }
            addText(toks, run, style);
        }

        bool startsWith(const std::string &s, size_t at, const char *p)
        {
            const size_t n = std::char_traits<char>::length(p);
            return s.size() >= at + n && s.compare(at, n, p) == 0;
        }

        /** The inline tags an agent's Markdown carries; anything else in <…> is text (`<model>`). */
        bool htmlTag(const std::string &s, size_t i, size_t &end, bool &isBreak)
        {
            static const char *tags[] = {"br", "b", "strong", "i", "em", "u", "code", "kbd", "sub", "sup", "span", "small", "mark"};
            const size_t close = s.find('>', i);
            if (close == std::string::npos || close - i > 24) return false;
            std::string name = s.substr(i + 1, close - i - 1);
            if (!name.empty() && name[0] == '/') name.erase(0, 1);
            while (!name.empty() && (name.back() == '/' || name.back() == ' ')) name.pop_back();
            const size_t sp = name.find(' ');
            if (sp != std::string::npos) name = name.substr(0, sp);
            for (char &c : name) c = (char)std::tolower((unsigned char)c);
            for (const char *t : tags)
                if (name == t)
                {
                    end = close + 1;
                    isBreak = name == "br";
                    return true;
                }
            return false;
        }

        void tokenize(const std::string &s, std::vector<Tok> &toks)
        {
            std::string text;   // plain text not yet added
            auto flush = [&] { addGlyphs(toks, text, Plain); text.clear(); };
            size_t i = 0;
            while (i < s.size())
            {
                const char c = s[i];
                if (c == '\\' && i + 1 < s.size() && isPunct(s[i + 1]))
                {
                    text += s[i + 1];
                    i += 2;
                    continue;
                }
                if (c == '\n')
                {
                    flush();
                    Tok b;
                    b.style = Break;
                    toks.push_back(b);
                    ++i;
                    continue;
                }
                if (c == '`')
                {
                    size_t n = 0;
                    while (i + n < s.size() && s[i + n] == '`') ++n;
                    const std::string fence(n, '`');
                    size_t close = s.find(fence, i + n);
                    while (close != std::string::npos && close + n < s.size() && s[close + n] == '`')
                        close = s.find(fence, close + n + 1);   // a longer run is not this span's closer
                    // Unclosed: a reply streaming in - code to the end, not a stray backtick (R-ASSIST-12).
                    std::string code = s.substr(i + n, close == std::string::npos ? std::string::npos : close - i - n);
                    if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ' && code.find_first_not_of(' ') != std::string::npos)
                        code = code.substr(1, code.size() - 2);
                    flush();
                    if (!code.empty()) addText(toks, code, Code);
                    i = close == std::string::npos ? s.size() : close + n;
                    continue;
                }
                if ((c == '[' || (c == '!' && i + 1 < s.size() && s[i + 1] == '[')))
                {
                    const size_t open = c == '!' ? i + 1 : i;
                    // The label may hold brackets of its own (`[a [b] c](u)`): match them.
                    int depth = 0;
                    size_t close = std::string::npos;
                    for (size_t j = open; j < s.size(); ++j)
                    {
                        if (s[j] == '\\') { ++j; continue; }
                        if (s[j] == '[') ++depth;
                        else if (s[j] == ']' && --depth == 0) { close = j; break; }
                    }
                    if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(')
                    {
                        const size_t end = s.find(')', close + 2);
                        if (end != std::string::npos)
                        {
                            flush();
                            std::vector<Tok> inner;
                            tokenize(s.substr(open + 1, close - open - 1), inner);
                            for (Tok &t : inner)
                            {
                                if (!t.delim) t.style |= Link;
                                toks.push_back(t);
                            }
                            i = end + 1;
                            continue;
                        }
                    }
                    text += c;
                    ++i;
                    continue;
                }
                if (c == '<')
                {
                    const size_t close = s.find('>', i);
                    if (close != std::string::npos && (startsWith(s, i + 1, "http://") || startsWith(s, i + 1, "https://") ||
                                                       startsWith(s, i + 1, "mailto:")) &&
                        s.find(' ', i) > close)
                    {
                        flush();
                        addText(toks, s.substr(i + 1, close - i - 1), Link);
                        i = close + 1;
                        continue;
                    }
                    size_t end = 0;
                    bool br = false;
                    if (htmlTag(s, i, end, br))
                    {
                        if (br)
                        {
                            flush();
                            Tok b;
                            b.style = Break;
                            toks.push_back(b);
                        }
                        i = end;
                        continue;
                    }
                }
                if ((c == 'h') && (i == 0 || isSpace(s[i - 1]) || s[i - 1] == '(') &&
                    (startsWith(s, i, "http://") || startsWith(s, i, "https://")))
                {
                    size_t end = i;
                    while (end < s.size() && !isSpace(s[end]) && s[end] != '<') ++end;
                    while (end > i && std::string(".,;:!?'\"").find(s[end - 1]) != std::string::npos) --end;
                    if (end > i && s[end - 1] == ')' && s.substr(i, end - i).find('(') == std::string::npos) --end;
                    flush();
                    addText(toks, s.substr(i, end - i), Link);
                    i = end;
                    continue;
                }
                if (c == '*' || c == '_' || c == '~')
                {
                    size_t n = 0;
                    while (i + n < s.size() && s[i + n] == c) ++n;
                    if (c == '~' && n != 2)
                    {
                        text.append(n, c);
                        i += n;
                        continue;
                    }
                    const char prev = i == 0 ? ' ' : s[i - 1];
                    const char next = i + n >= s.size() ? ' ' : s[i + n];
                    const bool left = !isSpace(next) && (!isPunct(next) || isSpace(prev) || isPunct(prev));
                    const bool right = !isSpace(prev) && (!isPunct(prev) || isSpace(next) || isPunct(next));
                    Tok d;
                    d.delim = true;
                    d.ch = c;
                    d.count = d.orig = (int)n;
                    if (c == '_')
                    {
                        d.canOpen = left && (!right || isPunct(prev));
                        d.canClose = right && (!left || isPunct(next));
                    }
                    else
                    {
                        d.canOpen = left;
                        d.canClose = right;
                    }
                    flush();
                    toks.push_back(d);
                    i += n;
                    continue;
                }
                text += c;
                ++i;
            }
            flush();
        }

        /** CommonMark's emphasis matching, the part of it agents' replies use. */
        void resolveEmphasis(std::vector<Tok> &toks)
        {
            for (size_t c = 0; c < toks.size(); ++c)
            {
                Tok &cl = toks[c];
                if (!cl.delim || !cl.canClose) continue;
                while (cl.count > 0)
                {
                    long o = (long)c - 1;
                    for (; o >= 0; --o)
                    {
                        const Tok &op = toks[(size_t)o];
                        if (!op.delim || op.ch != cl.ch || !op.canOpen || op.count <= 0) continue;
                        // The rule of three: `*a**b*` is not `a` emphasised then `b`.
                        if ((op.canClose || cl.canOpen) && (op.orig + cl.orig) % 3 == 0 && !(op.orig % 3 == 0 && cl.orig % 3 == 0))
                            continue;
                        if (cl.ch == '~' && op.count < 2) continue;
                        break;
                    }
                    if (o < 0) break;
                    Tok &op = toks[(size_t)o];
                    const int use = cl.ch == '~' ? 2 : (op.count >= 2 && cl.count >= 2 ? 2 : 1);
                    const uint16_t st = cl.ch == '~' ? Strike : use == 2 ? Bold : Italic;
                    for (size_t k = (size_t)o + 1; k < c; ++k)
                    {
                        toks[k].style |= st;
                        if (toks[k].delim) toks[k].canOpen = toks[k].canClose = false;   // left as literal text
                    }
                    op.count -= use;
                    cl.count -= use;
                }
            }
            // A reply streams in (R-ASSIST-9): `**Expos` is bold being typed, not two stars. An opener
            // that nothing closed, and that cannot itself close (so not `2**3`), styles to the end.
            for (size_t o = 0; o < toks.size(); ++o)
            {
                Tok &op = toks[o];
                if (!op.delim || !op.canOpen || op.canClose || op.count < 2) continue;
                const uint16_t st = op.ch == '~' ? Strike : Bold;
                for (size_t k = o + 1; k < toks.size(); ++k) toks[k].style |= st;
                op.count -= 2;
            }
        }

        std::vector<Span> toSpans(const std::vector<Tok> &toks)
        {
            std::vector<Span> out;
            for (const Tok &t : toks)
            {
                if (t.style & Break)
                {
                    out.push_back({std::string(), Break});
                    continue;
                }
                const std::string text = t.delim ? std::string((size_t)std::max(0, t.count), t.ch) : t.text;
                if (text.empty()) continue;
                const bool own = t.style & (Tick | Cross | Symbol);
                if (!own && !out.empty() && out.back().style == t.style) out.back().text += text;
                else out.push_back({text, t.style});
            }
            return out;
        }

        // ── blocks ──

        size_t indentOf(const std::string &l)
        {
            size_t n = 0;
            while (n < l.size() && l[n] == ' ') ++n;
            return n;
        }
        bool blank(const std::string &l) { return l.find_first_not_of(' ') == std::string::npos; }

        bool isRule(const std::string &t)
        {
            if (t.empty() || (t[0] != '-' && t[0] != '*' && t[0] != '_')) return false;
            int n = 0;
            for (char c : t)
            {
                if (c == t[0]) ++n;
                else if (c != ' ') return false;
            }
            return n >= 3;
        }

        bool fenceAt(const std::string &l, char &ch, size_t &len)
        {
            const size_t ind = indentOf(l);
            if (ind > 3 || ind >= l.size() || (l[ind] != '`' && l[ind] != '~')) return false;
            ch = l[ind];
            len = 0;
            while (ind + len < l.size() && l[ind + len] == ch) ++len;
            if (len < 3) return false;
            return ch == '~' || l.find('`', ind + len) == std::string::npos;
        }

        /** "- x" "* x" "+ x" -> content start, or npos. */
        size_t bulletAt(const std::string &l)
        {
            const size_t ind = indentOf(l);
            if (ind >= l.size() || (l[ind] != '-' && l[ind] != '*' && l[ind] != '+')) return std::string::npos;
            if (ind + 1 < l.size() && l[ind + 1] != ' ') return std::string::npos;
            size_t at = ind + 1;
            while (at < l.size() && l[at] == ' ') ++at;
            return at;
        }

        /** "12. x" "3) x" -> content start (and the marker), or npos. */
        size_t orderedAt(const std::string &l, std::string &marker)
        {
            const size_t ind = indentOf(l);
            size_t d = ind;
            while (d < l.size() && std::isdigit((unsigned char)l[d]) && d - ind < 9) ++d;
            if (d == ind || d >= l.size() || (l[d] != '.' && l[d] != ')')) return std::string::npos;
            if (d + 1 < l.size() && l[d + 1] != ' ') return std::string::npos;
            marker = l.substr(ind, d - ind) + ".";
            size_t at = d + 1;
            while (at < l.size() && l[at] == ' ') ++at;
            return at;
        }

        bool isDelimRow(const std::string &l)
        {
            if (l.find('-') == std::string::npos) return false;
            bool any = false;
            for (char c : l)
            {
                if (c == '-') any = true;
                else if (c != '|' && c != ':' && c != ' ') return false;
            }
            return any && l.find('|') != std::string::npos;
        }

        std::vector<std::string> splitCells(std::string l)
        {
            const size_t a = l.find_first_not_of(' ');
            const size_t b = l.find_last_not_of(' ');
            l = a == std::string::npos ? std::string() : l.substr(a, b - a + 1);
            if (!l.empty() && l.front() == '|') l.erase(0, 1);
            if (!l.empty() && l.back() == '|' && (l.size() < 2 || l[l.size() - 2] != '\\')) l.pop_back();
            std::vector<std::string> cells;
            std::string cur;
            bool code = false;
            for (size_t i = 0; i < l.size(); ++i)
            {
                if (l[i] == '\\' && i + 1 < l.size() && l[i + 1] == '|') { cur += '|'; ++i; continue; }
                if (l[i] == '`') code = !code;
                if (l[i] == '|' && !code) { cells.push_back(cur); cur.clear(); continue; }
                cur += l[i];
            }
            cells.push_back(cur);
            for (auto &c : cells)
            {
                const size_t x = c.find_first_not_of(' '), y = c.find_last_not_of(' ');
                c = x == std::string::npos ? std::string() : c.substr(x, y - x + 1);
            }
            return cells;
        }

        bool startsBlock(const std::vector<std::string> &lines, size_t j)
        {
            const std::string &l = lines[j];
            const size_t ind = indentOf(l);
            char fc;
            size_t fl;
            std::string mk;
            if (fenceAt(l, fc, fl)) return true;
            if (ind <= 3 && ind < l.size())
            {
                const std::string t = l.substr(ind);
                if (t[0] == '#')
                {
                    size_t n = 0;
                    while (n < t.size() && t[n] == '#') ++n;
                    if (n <= 6 && (n == t.size() || t[n] == ' ')) return true;
                }
                if (t[0] == '>' || isRule(t)) return true;
            }
            if (bulletAt(l) != std::string::npos || orderedAt(l, mk) != std::string::npos) return true;
            return l.find('|') != std::string::npos && j + 1 < lines.size() && isDelimRow(lines[j + 1]);
        }

        std::vector<Span> inlineOf(const std::string &s) { return parseInline(s); }

        void appendLine(std::vector<Span> &spans, const std::string &line)
        {
            if (!spans.empty()) spans.push_back({std::string(), Break});
            for (Span &sp : inlineOf(line)) spans.push_back(std::move(sp));
        }
    }

    std::vector<Span> parseInline(const std::string &text)
    {
        std::vector<Tok> toks;
        tokenize(normalise(text), toks);   // idempotent: parse() has already done it for a block
        resolveEmphasis(toks);
        return toSpans(toks);
    }

    std::string plain(const std::string &text)
    {
        std::string out;
        for (const Span &s : parseInline(text))
            out += (s.style & Break) ? std::string(" ") : (s.style & Tick) ? std::string("\xC2\xB7") : (s.style & Cross) ? std::string("x") : s.text;
        return out;
    }

    std::vector<Block> parse(const std::string &text)
    {
        std::string s = normalise(text);
        // Reasoning is not the answer: a closed <think> goes; an open one is "Thinking…" (R-ASSIST-12).
        bool thinking = false;
        {
            const size_t open = s.find("<think>"), close = s.find("</think>");
            if (close != std::string::npos && (open == std::string::npos || open > close))
                s.erase(0, close + 8);   // the template opened it before the reply began
        }
        for (;;)
        {
            const size_t a = s.find("<think>");
            if (a == std::string::npos) break;
            const size_t b = s.find("</think>", a);
            if (b == std::string::npos)
            {
                s.erase(a);
                thinking = true;
                break;
            }
            s.erase(a, b + 8 - a);
        }

        std::vector<std::string> lines;
        for (size_t at = 0;;)
        {
            const size_t nl = s.find('\n', at);
            lines.push_back(s.substr(at, nl == std::string::npos ? std::string::npos : nl - at));
            if (nl == std::string::npos) break;
            at = nl + 1;
        }

        std::vector<Block> out;
        size_t i = 0;
        while (i < lines.size())
        {
            const std::string &line = lines[i];
            if (blank(line)) { ++i; continue; }
            const size_t ind = indentOf(line);
            const std::string t = line.substr(ind);
            char fch;
            size_t flen;
            std::string mk;
            if (fenceAt(line, fch, flen))
            {
                Block b;
                b.kind = BlockKind::CodeBlock;
                ++i;
                for (; i < lines.size(); ++i)
                {
                    char c2;
                    size_t l2;
                    if (fenceAt(lines[i], c2, l2) && c2 == fch && l2 >= flen && blank(lines[i].substr(indentOf(lines[i]) + l2)))
                    {
                        ++i;
                        break;
                    }
                    const size_t strip = std::min(ind, indentOf(lines[i]));
                    b.code.push_back(lines[i].substr(strip));
                }
                // A fence still streaming in leaves a trailing empty line: not part of the code.
                while (!b.code.empty() && blank(b.code.back()) && i >= lines.size()) b.code.pop_back();
                out.push_back(std::move(b));
                continue;
            }
            if (ind <= 3 && t[0] == '#')
            {
                size_t n = 0;
                while (n < t.size() && t[n] == '#') ++n;
                if (n <= 6 && (n == t.size() || t[n] == ' '))
                {
                    std::string h = n < t.size() ? t.substr(n + 1) : std::string();
                    size_t e = h.find_last_not_of(' ');
                    h = e == std::string::npos ? std::string() : h.substr(0, e + 1);
                    size_t hs = h.size();
                    while (hs > 0 && h[hs - 1] == '#') --hs;   // closing #s, when a space precedes them
                    if (hs < h.size() && (hs == 0 || h[hs - 1] == ' ')) h = h.substr(0, hs);
                    e = h.find_last_not_of(' ');
                    h = e == std::string::npos ? std::string() : h.substr(0, e + 1);
                    Block b;
                    b.kind = BlockKind::Heading;
                    b.level = (int)n;
                    b.spans = parseInline(h);
                    out.push_back(std::move(b));
                    ++i;
                    continue;
                }
            }
            if (ind <= 3 && isRule(t))
            {
                Block b;
                b.kind = BlockKind::Rule;
                out.push_back(std::move(b));
                ++i;
                continue;
            }
            if (line.find('|') != std::string::npos && i + 1 < lines.size() && isDelimRow(lines[i + 1]))
            {
                Block b;
                b.kind = BlockKind::Table;
                const std::vector<std::string> head = splitCells(line);
                for (const std::string &d : splitCells(lines[i + 1]))
                {
                    const bool l = !d.empty() && d.front() == ':', r = !d.empty() && d.back() == ':';
                    b.align.push_back(l && r ? 'c' : r ? 'r' : 'l');
                }
                b.align.resize(head.size(), 'l');
                auto row = [&](const std::vector<std::string> &cells) {
                    std::vector<std::vector<Span>> r;
                    for (size_t c = 0; c < head.size(); ++c) r.push_back(c < cells.size() ? parseInline(cells[c]) : std::vector<Span>{});
                    b.rows.push_back(std::move(r));
                };
                row(head);
                i += 2;
                for (; i < lines.size() && !blank(lines[i]) && lines[i].find('|') != std::string::npos; ++i) row(splitCells(lines[i]));
                out.push_back(std::move(b));
                continue;
            }
            if (ind <= 3 && t[0] == '>')
            {
                Block b;
                b.kind = BlockKind::Quote;
                for (; i < lines.size(); ++i)
                {
                    const size_t qi = indentOf(lines[i]);
                    if (qi > 3 || qi >= lines[i].size() || lines[i][qi] != '>') break;
                    std::string q = lines[i].substr(qi);
                    while (!q.empty() && (q[0] == '>' || q[0] == ' ')) q.erase(0, 1);   // nesting is flattened
                    appendLine(b.spans, q);
                }
                out.push_back(std::move(b));
                continue;
            }
            const size_t bul = bulletAt(line);
            const size_t ord = bul == std::string::npos ? orderedAt(line, mk) : std::string::npos;
            if (bul != std::string::npos || ord != std::string::npos)
            {
                Block b;
                b.level = (int)std::min<size_t>(3, ind / 2);
                std::string content = line.substr(bul != std::string::npos ? bul : ord);
                if (bul != std::string::npos)
                {
                    b.kind = BlockKind::Bullet;
                    if (content.size() >= 3 && content[0] == '[' && content[2] == ']' && (content[1] == ' ' || content[1] == 'x' || content[1] == 'X') &&
                        (content.size() == 3 || content[3] == ' '))
                    {
                        b.kind = BlockKind::Task;
                        b.done = content[1] != ' ';
                        content = content.size() > 4 ? content.substr(4) : std::string();
                    }
                }
                else
                {
                    b.kind = BlockKind::Ordered;
                    b.marker = mk;
                }
                appendLine(b.spans, content);
                ++i;
                // An indented line under an item that does not start an item is more of that item.
                for (; i < lines.size() && !blank(lines[i]) && indentOf(lines[i]) > ind && !startsBlock(lines, i); ++i)
                    appendLine(b.spans, lines[i].substr(indentOf(lines[i])));
                out.push_back(std::move(b));
                continue;
            }
            Block b;
            b.kind = BlockKind::Paragraph;
            appendLine(b.spans, t);
            ++i;
            for (; i < lines.size() && !blank(lines[i]) && !startsBlock(lines, i); ++i) appendLine(b.spans, lines[i].substr(indentOf(lines[i])));
            out.push_back(std::move(b));
        }
        if (thinking)
        {
            Block b;
            b.kind = BlockKind::Thinking;
            b.spans = {{"Thinking\xE2\x80\xA6", Italic}};
            out.push_back(std::move(b));
        }
        return out;
    }

    // ── layout ──

    namespace
    {
        constexpr double kChipPad = 3.25;      // ladder 1: a code span's chip, each side
        constexpr double kBlockGap = 6.5;      // ladder 2: between paragraphs
        constexpr double kItemGap = 3.25;      // ladder 1: between items of one list
        constexpr double kHeadGap = 9.75;      // ladder 3: above a heading
        constexpr double kIndent = 13.0;       // ladder 4: one list level
        constexpr double kQuoteIndent = 9.75;  // ladder 3
        constexpr double kCodePad = 6.5;       // ladder 2: inside a code block
        constexpr double kCellGap = 9.75;      // ladder 3: between table columns
        constexpr double kRowPad = 3.25;       // ladder 1: between table rows

        struct Atom
        {
            std::string text;
            uint16_t style = Plain;
            bool space = false;   // a space precedes it
        };

        std::vector<Atom> atomsOf(const std::vector<Span> &spans, uint16_t extra = Plain)
        {
            std::vector<Atom> out;
            bool space = false;
            for (const Span &sp : spans)
            {
                const uint16_t st = sp.style | extra;
                if (sp.style & Break)
                {
                    out.push_back({std::string(), Break, false});
                    space = false;
                    continue;
                }
                if (st & (Tick | Cross | Symbol))
                {
                    out.push_back({sp.text, st, space});
                    space = false;
                    continue;
                }
                size_t i = 0;
                while (i < sp.text.size())
                {
                    if (sp.text[i] == ' ')
                    {
                        space = true;
                        ++i;
                        continue;
                    }
                    const size_t e = sp.text.find(' ', i);
                    out.push_back({sp.text.substr(i, e == std::string::npos ? std::string::npos : e - i), st, space});
                    space = false;
                    i = e == std::string::npos ? sp.text.size() : e;
                }
            }
            return out;
        }

        struct Face
        {
            const char *font;
            double px;
        };

        Face faceOf(uint16_t st, const char *baseFont, double px)
        {
            if (st & (Code | Symbol)) return {font::mono(), px - 1.0};
            if (st & Italic) return {font::sansItalic(), px};   // no bold italic face: italic carries it
            if (st & Bold) return {font::sansSemiBold(), px};
            return {baseFont, px};
        }

        Ink inkOf(uint16_t st, Ink base)
        {
            if (st & Tick) return Ink::Success;
            if (st & Cross) return Ink::Danger;
            if (st & Link) return Ink::Link;
            if (st & Strike) return Ink::Muted;
            return base;
        }

        struct Measurer
        {
            const IRenderTarget &t;
            struct Key { const char *f; double px; double w; };
            std::vector<Key> spaces;
            double text(const std::string &s, const Face &f) const { return t.measureText(s, f.px, f.font); }
            double space(const Face &f)
            {
                for (const Key &k : spaces)
                    if (k.f == f.font && k.px == f.px) return k.w;
                spaces.push_back({f.font, f.px, t.measureText(" ", f.px, f.font)});
                return spaces.back().w;
            }
        };

        struct Line
        {
            std::vector<Run> runs;
            double w = 0;
        };

        /** Splits a word wider than the line where it stops fitting, at codepoint boundaries. */
        std::vector<std::string> breakWord(const Measurer &m, std::string word, const Face &f, double maxW)
        {
            std::vector<std::string> out;
            while (word.size() > 1 && m.text(word, f) > maxW)
            {
                size_t cut = word.size();
                while (cut > 1 && m.text(word.substr(0, cut), f) > maxW)
                {
                    --cut;
                    while (cut > 1 && ((unsigned char)word[cut] & 0xC0) == 0x80) --cut;
                }
                out.push_back(word.substr(0, cut));
                word = word.substr(cut);
            }
            out.push_back(word);
            return out;
        }

        /** Greedy fill of styled atoms into lines no wider than `width`; runs of one style on one
         *  line are merged, so a line is a handful of drawText calls, not one per word. */
        std::vector<Line> fill(Measurer &m, const std::vector<Atom> &atoms, double width, const char *baseFont, double px, Ink baseInk)
        {
            std::vector<Line> lines(1);
            double x = 0.0;
            auto place = [&](const std::string &text, uint16_t st, bool space) {
                const Face f = faceOf(st, baseFont, px);
                const bool mark = st & (Tick | Cross);
                const double w = mark ? px * 0.8 : m.text(text, f);
                const bool chip = st & Code;
                Line &ln = lines.back();
                Run *last = ln.runs.empty() ? nullptr : &ln.runs.back();
                const bool joinChip = chip && last && (last->style & Code) && last->style == st;
                // The gap between words is the TEXT face's, even before a code chip or an arrow drawn in mono.
                const double sp = (space && x > 0) ? m.space(joinChip ? f : faceOf(st & ~(Code | Symbol), baseFont, px)) : 0.0;
                const double need = joinChip ? sp + w : sp + w + (chip ? 2 * kChipPad : 0.0);
                if (x > 0 && x + need > width)
                {
                    lines.emplace_back();
                    x = 0.0;
                    return false;
                }
                Line &cur = lines.back();
                Run *prev = cur.runs.empty() ? nullptr : &cur.runs.back();
                if (joinChip && prev)
                {
                    prev->text += (sp > 0 ? " " : "") + text;
                    prev->w = (x - kChipPad + sp + w) - prev->x;
                    x = prev->x + prev->w + kChipPad;
                }
                else if (prev && !mark && !chip && prev->style == st && prev->font == f.font)
                {
                    prev->text += (sp > 0 ? " " : "") + text;
                    prev->w = x + sp + w - prev->x;
                    x += sp + w;
                }
                else
                {
                    Run r;
                    r.text = mark ? std::string() : text;
                    r.x = x + sp + (chip ? kChipPad : 0.0);
                    r.w = w;
                    r.px = f.px;
                    r.font = f.font;
                    r.ink = inkOf(st, baseInk);
                    r.style = st;
                    cur.runs.push_back(r);
                    x = r.x + w + (chip ? kChipPad : 0.0);
                }
                cur.w = x;
                return true;
            };
            for (const Atom &a : atoms)
            {
                if (a.style & Break)
                {
                    lines.emplace_back();
                    x = 0.0;
                    continue;
                }
                const Face f = faceOf(a.style, baseFont, px);
                const double pad = (a.style & Code) ? 2 * kChipPad : 0.0;
                if (!(a.style & (Tick | Cross)) && m.text(a.text, f) + pad > width)
                {
                    // R5: a word (a path, a URL) wider than the line is cut where it stops fitting.
                    bool first = true;
                    for (const std::string &piece : breakWord(m, a.text, f, std::max(8.0, width - pad)))
                    {
                        if (!place(piece, a.style, first && a.space)) place(piece, a.style, false);
                        first = false;
                    }
                    continue;
                }
                if (!place(a.text, a.style, a.space)) place(a.text, a.style, false);
            }
            return lines;
        }

        void emit(Layout &out, const std::vector<Line> &lines, double x0, double &y, double lineH, double basePx, double width = -1,
                  char align = 'l')
        {
            for (const Line &ln : lines)
            {
                const double off = width < 0 || align == 'l' ? 0.0 : align == 'r' ? width - ln.w : (width - ln.w) * 0.5;
                const double baseline = y + lineH * 0.5 + basePx * 0.35;
                for (Run r : ln.runs)
                {
                    r.x += x0 + off;
                    r.baseline = baseline;
                    out.runs.push_back(std::move(r));
                }
                y += lineH;
            }
        }

        bool listKind(BlockKind k) { return k == BlockKind::Bullet || k == BlockKind::Ordered || k == BlockKind::Task; }
    }

    Layout layout(const IRenderTarget &t, const std::vector<Block> &blocks, double width, const Metrics &mt)
    {
        Layout out;
        Measurer m{t, {}};
        width = std::max(24.0, width);
        double y = 0.0;
        for (size_t bi = 0; bi < blocks.size(); ++bi)
        {
            const Block &b = blocks[bi];
            if (bi > 0)
                y += listKind(b.kind) && listKind(blocks[bi - 1].kind) ? kItemGap : b.kind == BlockKind::Heading ? kHeadGap : kBlockGap;
            switch (b.kind)
            {
            case BlockKind::Paragraph:
            case BlockKind::Thinking:
            {
                const Ink ink = b.kind == BlockKind::Thinking ? Ink::Muted : Ink::Body;
                emit(out, fill(m, atomsOf(b.spans), width, font::sans(), mt.px, ink), 0.0, y, mt.lineH, mt.px);
                break;
            }
            case BlockKind::Heading:
            {
                // The type ramp: 13 SemiBold (a dialog section title), 12, then the body size in SemiBold.
                const double px = b.level == 1 ? 13.0 : b.level == 2 ? 12.0 : mt.px;
                emit(out, fill(m, atomsOf(b.spans), width, font::sansSemiBold(), px, Ink::Body), 0.0, y, px * 1.42, px);
                break;
            }
            case BlockKind::Bullet:
            case BlockKind::Ordered:
            case BlockKind::Task:
            {
                const double indent = b.level * kIndent;
                double gutter = kIndent;
                if (b.kind == BlockKind::Ordered) gutter = std::max(16.25, t.measureText(b.marker, mt.px, font::sans()) + 4.875);
                if (b.kind == BlockKind::Task) gutter = 16.25;   // ladder 5
                const double cy = y + mt.lineH * 0.5;
                if (b.kind == BlockKind::Bullet)
                    out.decos.push_back({Deco::Kind::Bullet, Rect{indent + 2.0, cy - 1.75, 3.5, 3.5}, b.level == 0});
                else if (b.kind == BlockKind::Task)
                    out.decos.push_back({Deco::Kind::TaskBox, Rect{indent, cy - 4.875, 9.75, 9.75}, b.done});
                else
                {
                    Run r;
                    r.text = b.marker;
                    r.x = indent;
                    r.baseline = cy + mt.px * 0.35;
                    r.w = t.measureText(b.marker, mt.px, font::sans());
                    r.px = mt.px;
                    r.font = font::sans();
                    r.ink = Ink::Muted;
                    out.runs.push_back(r);
                }
                const Ink ink = b.kind == BlockKind::Task && b.done ? Ink::Muted : Ink::Body;
                emit(out, fill(m, atomsOf(b.spans), width - indent - gutter, font::sans(), mt.px, ink), indent + gutter, y, mt.lineH, mt.px);
                break;
            }
            case BlockKind::Quote:
            {
                const double top = y;
                emit(out, fill(m, atomsOf(b.spans), width - kQuoteIndent, font::sans(), mt.px, Ink::Quote), kQuoteIndent, y, mt.lineH, mt.px);
                out.decos.push_back({Deco::Kind::QuoteBar, Rect{0.0, top + 1.625, 2.0, y - top - 3.25}, false});
                break;
            }
            case BlockKind::CodeBlock:
            {
                const double top = y;
                const double inner = width - 2 * kCodePad;
                const Face f{font::mono(), mt.codePx};
                y += kCodePad;
                for (const std::string &cl : b.code)
                {
                    // Code keeps its spaces and is cut by width, not by words (R5): a long line wraps.
                    std::vector<std::string> pieces = cl.empty() ? std::vector<std::string>{std::string()} : breakWord(m, cl, f, inner);
                    for (const std::string &p : pieces)
                    {
                        Run r;
                        r.text = p;
                        r.x = kCodePad;
                        r.baseline = y + mt.codeLineH * 0.5 + mt.codePx * 0.35;
                        r.w = p.empty() ? 0.0 : m.text(p, f);
                        r.px = mt.codePx;
                        r.font = font::mono();
                        r.ink = Ink::Body;
                        out.runs.push_back(r);
                        y += mt.codeLineH;
                    }
                }
                y += kCodePad;
                out.decos.push_back({Deco::Kind::CodeBox, Rect{0.0, top, width, y - top}, false});
                break;
            }
            case BlockKind::Rule:
                y += kItemGap;
                out.decos.push_back({Deco::Kind::Rule, Rect{0.0, y, width, 1.0}, false});
                y += kItemGap + 1.0;
                break;
            case BlockKind::Table:
            {
                const size_t cols = b.align.size();
                if (cols == 0) break;
                // Each column's natural (one-line) width; if they do not all fit, the narrow ones keep
                // theirs and the wide ones share what is left, wrapping inside their cell (R5).
                std::vector<double> natural(cols, 0.0);
                for (size_t r = 0; r < b.rows.size(); ++r)
                    for (size_t c = 0; c < cols; ++c)
                    {
                        const auto lines = fill(m, atomsOf(b.rows[r][c], r == 0 ? Bold : Plain), 1e9, font::sans(), mt.px, Ink::Body);
                        for (const Line &ln : lines) natural[c] = std::max(natural[c], ln.w);
                    }
                const double avail = width - kCellGap * (double)(cols - 1);
                std::vector<double> colW = natural;
                double total = 0.0;
                for (double w : natural) total += w;
                if (total > avail)
                {
                    std::vector<size_t> order(cols);
                    for (size_t c = 0; c < cols; ++c) order[c] = c;
                    std::sort(order.begin(), order.end(), [&](size_t a, size_t z) { return natural[a] < natural[z]; });
                    double left = std::max(avail, 26.0 * (double)cols);
                    size_t remaining = cols;
                    for (size_t c : order)
                    {
                        colW[c] = std::max(26.0, std::min(natural[c], left / (double)remaining));
                        left -= colW[c];
                        --remaining;
                    }
                }
                std::vector<double> colX(cols, 0.0);
                for (size_t c = 1; c < cols; ++c) colX[c] = colX[c - 1] + colW[c - 1] + kCellGap;
                for (size_t r = 0; r < b.rows.size(); ++r)
                {
                    double rowH = 0.0;
                    std::vector<std::vector<Line>> cells(cols);
                    for (size_t c = 0; c < cols; ++c)
                    {
                        cells[c] = fill(m, atomsOf(b.rows[r][c], r == 0 ? Bold : Plain), colW[c], font::sans(), mt.px, Ink::Body);
                        rowH = std::max(rowH, cells[c].size() * mt.lineH);
                    }
                    for (size_t c = 0; c < cols; ++c)
                    {
                        double cy = y;
                        emit(out, cells[c], colX[c], cy, mt.lineH, mt.px, colW[c], b.align[c]);
                    }
                    y += rowH;
                    if (r == 0) out.decos.push_back({Deco::Kind::TableLine, Rect{0.0, y + kRowPad * 0.5, colX[cols - 1] + colW[cols - 1], 1.0}, false});
                    y += kRowPad;
                }
                y -= kRowPad;
                break;
            }
            }
        }
        out.height = y;
        return out;
    }

    Inks themeInks(double alpha)
    {
        auto a = [alpha](Color c) { c.a *= alpha; return c; };
        Inks k;
        k.body = a(palette::foreground());
        k.muted = a(palette::mutedForeground());
        k.quote = a(palette::secondaryForeground());
        k.link = a(palette::primary());
        k.success = a(palette::success());
        k.danger = a(palette::destructive());
        k.chip = a(palette::secondary());
        k.codeBox = a(palette::background());
        k.line = a(palette::border());
        return k;
    }

    void paint(IRenderTarget &t, const Layout &l, double x, double y, const Inks &k, double clipTop, double clipBottom)
    {
        auto inkColor = [&](Ink i) {
            switch (i)
            {
            case Ink::Muted: return k.muted;
            case Ink::Quote: return k.quote;
            case Ink::Link: return k.link;
            case Ink::Success: return k.success;
            case Ink::Danger: return k.danger;
            default: return k.body;
            }
        };
        auto half = [](Color c, double f) { c.a *= f; return c; };
        for (const Deco &d : l.decos)
        {
            const Rect r{x + d.r.x, y + d.r.y, d.r.w, d.r.h};
            if (r.y > clipBottom || r.y + r.h < clipTop) continue;
            switch (d.kind)
            {
            case Deco::Kind::CodeBox:
                drawRoundedRect(t, r, radius::control(), Paint::filledStroked(k.codeBox, k.line, 1.0));
                break;
            case Deco::Kind::QuoteBar:
                drawRoundedRect(t, r, radius::pill(), Paint::filled(half(k.quote, 0.45)));
                break;
            case Deco::Kind::Rule:
            case Deco::Kind::TableLine:
                t.setStroke(d.kind == Deco::Kind::Rule ? k.line : half(k.quote, 0.25), 1.0);
                t.beginPath();
                t.moveTo(r.x, std::floor(r.y) + 0.5);
                t.lineTo(r.x + r.w, std::floor(r.y) + 0.5);
                t.strokePath();
                break;
            case Deco::Kind::Bullet:
                if (d.filled) drawCircle(t, r.x + r.w * 0.5, r.y + r.h * 0.5, r.w * 0.5, Paint::filled(k.quote));
                else drawCircle(t, r.x + r.w * 0.5, r.y + r.h * 0.5, r.w * 0.5 - 0.25, Paint::filledStroked(Color{0, 0, 0, 0}, k.quote, 1.0));
                break;
            case Deco::Kind::TaskBox:
                drawRoundedRect(t, r, radius::control(), d.filled ? Paint::filled(k.link) : Paint::filledStroked(Color{0, 0, 0, 0}, k.quote, 1.0));
                if (d.filled)
                {
                    t.setStroke(Color{1, 1, 1, k.link.a}, 1.4);
                    t.beginPath();
                    t.moveTo(r.x + r.w * 0.22, r.y + r.h * 0.52);
                    t.lineTo(r.x + r.w * 0.42, r.y + r.h * 0.72);
                    t.lineTo(r.x + r.w * 0.78, r.y + r.h * 0.30);
                    t.strokePath();
                }
                break;
            }
        }
        for (const Run &r : l.runs)
        {
            const double bx = x + r.x, by = y + r.baseline;
            if (by - r.px * 1.5 > clipBottom || by + r.px < clipTop) continue;   // culled (R6)
            const Color c = inkColor(r.ink);
            const double cy = by - r.px * 0.35;   // the line's centre, from cosmo's baseline rule
            if (r.style & (Tick | Cross))
            {
                const double s = r.w;
                t.setStroke(c, 1.5);
                t.beginPath();
                if (r.style & Tick)
                {
                    t.moveTo(bx + s * 0.12, cy + s * 0.02);
                    t.lineTo(bx + s * 0.40, cy + s * 0.30);
                    t.lineTo(bx + s * 0.92, cy - s * 0.30);
                }
                else
                {
                    t.moveTo(bx + s * 0.18, cy - s * 0.32);
                    t.lineTo(bx + s * 0.82, cy + s * 0.32);
                    t.moveTo(bx + s * 0.82, cy - s * 0.32);
                    t.lineTo(bx + s * 0.18, cy + s * 0.32);
                }
                t.strokePath();
                continue;
            }
            if (r.style & Code)
                drawRoundedRect(t, Rect{bx - kChipPad, cy - r.px * 0.5 - 2.0, r.w + 2 * kChipPad, r.px + 4.0}, radius::control(),
                                Paint::filled(k.chip));
            t.setFill(c);
            t.drawText(r.text, bx, by, r.px, r.font);
            if (r.style & (Link | Strike))
            {
                const double ly = (r.style & Strike) ? cy : by + 1.5;
                t.setStroke((r.style & Strike) ? c : half(c, 0.5), 1.0);
                t.beginPath();
                t.moveTo(bx, std::floor(ly) + 0.5);
                t.lineTo(bx + r.w, std::floor(ly) + 0.5);
                t.strokePath();
            }
        }
    }
}
}
}
