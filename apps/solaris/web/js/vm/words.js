/*
 * Solaris — vm/words.js: the window's words in the user's language (R-ACCESS-2) — strings/Strings.{h,cpp} and
 * app/Words.h, ported. The page draws in the language the window would: the model's `settings.language` resolved
 * against the browser's (as App resolves it against the OS's), from THE SAME tables (`apps/solaris/lang/*.strings`,
 * copied beside the page by `solaris-cc ntwb install` into web/lang/).
 *
 * Drawn words (Strings.h):
 *   tr("Settings")                     English is the key; the table's line, or the English when it has none.
 *   trc("menu", "Open")                one English word that means two things (key "menu|Open").
 *   trn(n, "{1} clip", "{1} clips")    a count picks the form by the language's plural rule.
 *   fillIn(tpl, ...args)               {1} … {9} by position (a language may reorder them).
 *   upper(s)                           Strings.cpp upper(): ASCII, Latin-1, Latin Ext-A, ơ ư, Vietnamese's ạ … ỹ.
 *   pseudo(en)                         en-XA: "[Šéţţîñĝš ···]", placeholders kept.
 * Stored words (Words.h) — a menu's labels, a toast, a dialog's lines kept to be shown later, in the language drawn
 * THEN: say("Undo {1}", label), sayn(n, one, other, ...args); shown(stored) turns them into words now; english(stored)
 * in English whatever is drawn (a key that must not change with the language). Anything not said that way is DATA and
 * is shown as it is (a strip's name, the service's sentence).
 *
 * Reactivity: tr/trc/trn/shown READ the `drawn` signal (and the tables' version), so an effect that draws words
 * re-runs when the language changes — exactly what App::relabel does once a pass.
 */
import { signal } from "../core/signal.js";

export const PLURAL_JOIN = "\x1F";
const SAY = "\x02", SAYN = "\x03", SEP = "\x1F", END = "\x04";

// ------------------------------------------------------------------ the tables
const tables = new Map();           // code -> {language: {code, name, plural}, entries: Map<key, forms[]>}
const caches = new Map();           // code -> Map<key, words>
export const drawn = signal("en");  // the language drawn now (a code with its table loaded, "en" or "en-XA")
export const version = signal(0);   // bumped when a table arrives

export function pluralForms(rule) { return rule === "one-other" ? 2 : rule === "other" ? 1 : 0; }
export function pluralIndex(rule, n) { return rule === "one-other" ? (n === 1 ? 0 : 1) : 0; }

function placeholders(s) {
  const out = new Set();
  for (let i = 0; i + 2 < s.length; ++i) if (s[i] === "{" && s[i + 1] >= "1" && s[i + 1] <= "9" && s[i + 2] === "}") out.add(s[i + 1]);
  return [...out].sort().join("");
}

/** Strings.cpp parseTable: a table's text → {language, entries}; throws "line N: why". */
export function parseTable(text) {
  const out = { language: { code: "", name: "", plural: "" }, entries: new Map() };
  const lines = String(text).split("\n");
  lines.forEach((raw, i) => {
    const lineNo = i + 1;
    const fail = (why) => { throw new Error("line " + lineNo + ": " + why); };
    const line = raw.trim();
    if (!line || line[0] === "#") return;
    if (line[0] === "@") {
      const sp = line.indexOf(" ");
      const k = line.slice(1, sp < 0 ? undefined : sp), v = sp < 0 ? "" : line.slice(sp).trim();
      if (!v) fail("@" + k + " names nothing");
      if (k === "language") out.language.code = v;
      else if (k === "name") out.language.name = v;
      else if (k === "plural") { if (!pluralForms(v)) fail("no plural rule `" + v + "` (one-other, other)"); out.language.plural = v; }
      else fail("no header `@" + k + "` (@language, @name, @plural)");
      return;
    }
    if (!out.language.code || !out.language.plural) fail("a string before @language and @plural");
    let at = 0;
    const quoted = () => {
      const got = [];
      for (;;) {
        while (at < line.length && /\s/.test(line[at])) ++at;
        if (at >= line.length || line[at] === "=" || line[at] === ";") { if (!got.length) fail("a string in quotes expected"); return got; }
        if (line[at] !== '"') fail("a string in quotes expected");
        let s = "", closed = false;
        ++at;
        while (at < line.length) {
          const c = line[at++];
          if (c === '"') { closed = true; break; }
          if (c === "\\" && at < line.length) { const e = line[at++]; s += e === "n" ? "\n" : e === "t" ? "\t" : e; } else s += c;
        }
        if (!closed) fail("a string never closed");
        got.push(s);
      }
    };
    const keys = quoted();
    if (at >= line.length || line[at] !== "=") fail("`=` expected after the English");
    ++at;
    const forms = quoted();
    if (at >= line.length || line[at] !== ";" || line.slice(at + 1).trim()) fail("`;` ends a line");
    if (keys.length > 2) fail("one English string, or a plural's two");
    const want = keys.length === 2 ? pluralForms(out.language.plural) : 1;
    if (forms.length !== want) fail(keys.length === 2 ? "a plural has " + want + " form(s) under @plural " + out.language.plural : "one translation for one English string");
    const en = keys[keys.length - 1];
    const bar = keys.length === 1 ? en.indexOf("|") : -1;
    const need = placeholders(bar < 0 ? en : en.slice(bar + 1));
    for (const f of forms) if (placeholders(f) !== need) fail("`" + f + "` must keep the placeholders of `" + en + "`");
    const key = keys.length === 2 ? keys[0] + PLURAL_JOIN + keys[1] : keys[0];
    if (out.entries.has(key)) fail("`" + en + "` twice");
    out.entries.set(key, forms);
  });
  if (!out.language.code) throw new Error("no @language");
  if (!out.language.plural) throw new Error("no @plural");
  if (!out.language.name) out.language.name = out.language.code;
  return out;
}

/** Fetch and parse lang/<code>.strings once (relative to the page). Resolves true when it is loaded. */
const loading = new Map();
export function loadTable(code) {
  if (code === "en" || code === "en-XA" || tables.has(code)) return Promise.resolve(true);
  if (loading.has(code)) return loading.get(code);
  const p = fetch(new URL("../../lang/" + encodeURIComponent(code) + ".strings", import.meta.url))
    .then((r) => { if (!r.ok) throw new Error(r.status + " " + r.statusText); return r.text(); })
    .then((t) => { const tb = parseTable(t); tables.set(code, tb); version.value = version.peek() + 1; return true; })
    .catch((e) => { console.warn("solaris: the words of " + code + " did not load:", e.message || e); return false; });
  loading.set(code, p);
  return p;
}

/** Strings.cpp resolve: "auto" → the browser's language when Solaris has it (vi-VN → vi), else "en"; a known code →
 *  itself; anything else → "en". `known` = the codes Solaris has (the model's settings.languages). */
export function resolve(setting, osLanguage, known) {
  const has = (c) => known.includes(c);
  if (setting !== "auto") return has(setting) ? setting : "en";
  let os = "";
  for (const c of String(osLanguage || "")) { if (c === "." || c === "@") break; os += c === "_" ? "-" : c; }
  if (has(os)) return os;
  const primary = os.split("-")[0];
  return has(primary) ? primary : "en";
}

/** The language drawn now; a table must be loaded first (vm does loadTable then setLanguage). */
export function setLanguage(code) { drawn.value = code === "en-XA" || tables.has(code) ? code : "en"; }

function cacheOf(code) { let c = caches.get(code); if (!c) caches.set(code, (c = new Map())); return c; }

function lookUp(key, en, form) {
  const code = drawn.value;
  void version.value; // a table arriving re-runs who drew
  const c = cacheOf(code);
  const hit = c.get(key);
  if (hit !== undefined) return hit;
  let words;
  if (code === "en-XA") words = pseudo(en);
  else {
    const t = tables.get(code);
    if (t) {
      const tableKey = form < 0 ? key : key.slice(0, key.length - 2);
      const e = t.entries.get(tableKey);
      words = e && Math.max(form, 0) < e.length ? e[Math.max(form, 0)] : en;
    } else words = en;
  }
  c.set(key, words);
  return words;
}

export function tr(en) { return lookUp(String(en), String(en), -1); }
export function trc(context, en) { return lookUp(context + "|" + en, String(en), -1); }
export function trn(n, one, other) {
  const code = drawn.value;
  const t = tables.get(code);
  const english = pluralIndex("one-other", n);
  const form = t ? pluralIndex(t.language.plural, n) : english;
  let key = one + PLURAL_JOIN + other + "\x1E" + String.fromCharCode(48 + form + (t ? 0 : 5));
  if (t) {
    if (!t.entries.has(key.slice(0, key.length - 2))) {
      key = key.slice(0, -1) + String.fromCharCode(53 + english);
      return lookUp(key, english === 0 ? one : other, -1);
    }
    return lookUp(key, english === 0 ? one : other, form);
  }
  return lookUp(key, english === 0 ? one : other, -1);
}

export function fillIn(tpl, ...args) {
  let out = "";
  for (let i = 0; i < tpl.length; ++i) {
    if (tpl[i] === "{" && i + 2 < tpl.length && tpl[i + 1] >= "1" && tpl[i + 1] <= "9" && tpl[i + 2] === "}" && tpl.charCodeAt(i + 1) - 49 < args.length) {
      out += String(args[tpl.charCodeAt(i + 1) - 49]);
      i += 2;
    } else out += tpl[i];
  }
  return out;
}

// ------------------------------------------------------------------ the pseudo-language
const ACCENT = { a: "å", c: "ç", d: "ď", e: "é", g: "ĝ", h: "ĥ", i: "î", j: "ĵ", k: "ķ", l: "ļ", n: "ñ", o: "ö", r: "ŕ", s: "š", t: "ţ", u: "ü",
                 w: "ŵ", y: "ý", z: "ž", A: "Å", C: "Ç", D: "Ď", E: "É", G: "Ĝ", H: "Ĥ", I: "Î", J: "Ĵ", K: "Ķ", L: "Ļ", N: "Ñ", O: "Ö",
                 R: "Ŕ", S: "Š", T: "Ţ", U: "Ü", W: "Ŵ", Y: "Ý", Z: "Ž" };
export function pseudo(en) {
  const cps = [...String(en)];
  let out = "[", points = 0;
  for (let i = 0; i < cps.length; ++i) {
    const c = cps[i];
    ++points;
    if (c === "{" && i + 2 < cps.length && cps[i + 1] >= "1" && cps[i + 1] <= "9" && cps[i + 2] === "}") { out += c + cps[i + 1] + cps[i + 2]; i += 2; continue; }
    out += ACCENT[c] || c;
  }
  return out + " " + "·".repeat(Math.max(1, Math.floor((points + 2) / 3))) + "]";
}

/** Strings.cpp upper(): the letters the languages use — and nothing else changed (ß stays ß, as the native's). */
export function upper(s) {
  let out = "";
  for (const ch of String(s)) {
    const cp = ch.codePointAt(0);
    let up = cp;
    if (cp >= 0x61 && cp <= 0x7a) up = cp - 32;
    else if (cp >= 0xe0 && cp <= 0xfe && cp !== 0xf7) up = cp - 0x20;
    else if (cp === 0xff) up = 0x178;
    else if ((cp >= 0x101 && cp <= 0x137 && (cp & 1)) || (cp >= 0x14b && cp <= 0x177 && (cp & 1))) up = cp - 1;
    else if ((cp >= 0x13a && cp <= 0x148 && !(cp & 1)) || (cp >= 0x17a && cp <= 0x17e && !(cp & 1))) up = cp - 1;
    else if (cp === 0x1a1 || cp === 0x1b0) up = cp - 1;
    else if (((cp >= 0x1e01 && cp <= 0x1e95) || (cp >= 0x1ea1 && cp <= 0x1eff)) && (cp & 1)) up = cp - 1;
    out += String.fromCodePoint(up);
  }
  return out;
}

// ------------------------------------------------------------------ Words.h: stored words
/** gettext's N_: marks an English literal kept to be translated where it is drawn. */
export const N_ = (en) => en;
/** English words to show later, in the language drawn then; {1} … the arguments (data). */
export function say(en, ...args) { return SAY + en + args.map((a) => SEP + String(a)).join("") + END; }
/** A count's words to show later: the form by the count in the language drawn then. */
export function sayn(n, one, other, ...args) { return SAYN + String(n) + SEP + one + SEP + other + args.map((a) => SEP + String(a)).join("") + END; }

function put(tpl, parts, from) {
  let out = "";
  for (let i = 0; i < tpl.length; ++i) {
    if (tpl[i] === "{" && i + 2 < tpl.length && tpl[i + 2] === "}" && tpl[i + 1] >= "1" && tpl[i + 1] <= "9" && from + (tpl.charCodeAt(i + 1) - 49) < parts.length) {
      out += parts[from + (tpl.charCodeAt(i + 1) - 49)];
      i += 2;
    } else out += tpl[i];
  }
  return out;
}

/** Stored words in the language drawn now (or, inEnglish, in English): each said piece translated, data as it is. */
export function shown(stored, inEnglish = false) {
  stored = String(stored ?? "");
  if (!stored.includes(SAY) && !stored.includes(SAYN)) return stored;
  let out = "", at = 0;
  while (at < stored.length) {
    const c = stored[at];
    if (c !== SAY && c !== SAYN) { out += c; ++at; continue; }
    const end = stored.indexOf(END, at);
    const piece = stored.slice(at + 1, end < 0 ? undefined : end);
    at = end < 0 ? stored.length : end + 1;
    const parts = piece.split(SEP);
    if (c === SAY) out += put(inEnglish ? parts[0] : tr(parts[0]), parts, 1);
    else if (parts.length >= 3) {
      const n = parseInt(parts[0], 10) || 0;
      out += put(inEnglish ? (n === 1 ? parts[1] : parts[2]) : trn(n, parts[1], parts[2]), parts, 3);
    }
  }
  return out;
}
export const english = (stored) => shown(stored, true);
