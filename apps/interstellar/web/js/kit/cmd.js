/*
 * Interstellar — kit/cmd.js: the one place a moved control becomes TEXT, as the window spells it.
 *
 * A port of app/widgets/CommandLine.{h,cpp} (namespace cmd): the page reports intent only as command
 * lines (vm.cmd), and a number or a name formatted two ways drifts the first time a key is added -
 * silently, because both spellings parse. So every widget module formats through here, exactly as
 * its native widget formats through cmd:: -
 *
 *   q(arg)               cmd::quote      double-quote when the argument holds whitespace (space, tab), a
 *                                        quote, or is empty; `"` and `\` escaped with `\`
 *   bindName(typed)      cmd::bindName   what a person typed, made a legal bind name (letters, digits,
 *                                        `_`; not starting with a digit -> "v_" prefix; at most 32)
 *   num(v)               cmd::num        seven significant digits (ostream precision 7 = printf %.7g);
 *                                        |v| < 1e-9 prints 0
 *   seconds(t, fps)      cmd::seconds    a time argument quantised to the frame grid (t < 0 -> 0)
 *   timecode(t, fps)     cmd::timecode   HH:MM:SS:FF (fps <= 0 -> 24)
 *   gradeSet(bind, filter, fields)  cmd::gradeSet  `set "b.f.k=v" …` (each address quoted when needed)
 *   setLine(address, value)          `set <address>=<value>`, the whole assignment one token (q)
 */

/** printf("%.<p>g", v) - what `std::ostream << double` prints at precision p. */
function fmtG(v, p) {
  if (!Number.isFinite(v)) return Number.isNaN(v) ? "nan" : v > 0 ? "inf" : "-inf";
  if (v === 0) return "0";
  const exp = Number(v.toExponential(p - 1).split("e")[1]);   // the exponent AFTER rounding to p digits
  if (exp < -4 || exp >= p) {
    let [mant, e] = v.toExponential(p - 1).split("e");
    if (mant.includes(".")) mant = mant.replace(/0+$/, "").replace(/\.$/, "");
    const n = Number(e);
    return mant + "e" + (n < 0 ? "-" : "+") + String(Math.abs(n)).padStart(2, "0");
  }
  let s = v.toFixed(Math.max(0, p - 1 - exp));
  if (s.includes(".")) s = s.replace(/0+$/, "").replace(/\.$/, "");
  return s === "-0" ? "0" : s;
}

/** cmd::num: seven significant digits; -0 and denormal noise print as 0 (CommandLine.cpp:12-19). */
export function num(v) {
  v = Number(v);
  if (Math.abs(v) < 1e-9) v = 0;
  return fmtG(v, 7);
}

/** cmd::seconds: quantised to the frame grid at fps, so a cut lands ON a frame (CommandLine.cpp:21-26). */
export function seconds(t, fps) {
  t = Number(t);
  if (!(t > 0)) t = 0;
  if (fps > 0) t = Math.round(t * fps) / fps;
  return num(t);
}

/** cmd::quote (CommandLine.cpp:42-56). */
export function q(arg) {
  const s = String(arg ?? "");
  if (/[\n\r]/.test(s)) throw new Error("a line break cannot be part of a command");
  const needs = s === "" || /[ \t"]/.test(s);
  if (!needs) return s;
  return '"' + s.replace(/["\\]/g, (c) => "\\" + c) + '"';
}

/** cmd::bindName (CommandLine.cpp:28-40): bytes that are ASCII letters/digits/_ kept; space, - and .
 *  become one _ (never leading, never doubled); anything else (é, —) dropped, as the native's
 *  byte-wise isalnum drops a UTF-8 byte. */
export function bindName(typed) {
  let b = "";
  for (const ch of new TextEncoder().encode(String(typed ?? ""))) {
    const c = String.fromCharCode(ch);
    if (/[A-Za-z0-9_]/.test(c)) b += c;
    else if ((c === " " || c === "-" || c === ".") && b && !b.endsWith("_")) b += "_";
  }
  b = b.replace(/_+$/, "");
  if (/^[0-9]/.test(b)) b = "v_" + b;
  return b.length > 32 ? b.slice(0, 32) : b;
}

/** cmd::timecode (CommandLine.cpp:66-78). */
export function timecode(t, fps) {
  t = Number(t);
  if (!(t > 0)) t = 0;
  if (!(fps > 0)) fps = 24;
  const frames = Math.floor(t * fps + 1e-6);
  const ifps = Math.round(fps);
  const ff = ifps > 0 ? frames % ifps : 0;
  const total = ifps > 0 ? Math.floor(frames / ifps) : 0;
  const p = (n) => String(n).padStart(2, "0");
  return `${p(Math.floor(total / 3600))}:${p(Math.floor(total / 60) % 60)}:${p(total % 60)}:${p(ff)}`;
}

/** cmd::gradeSet: `set b.f.k=v b.f.k2=v2 …` - one line, several addresses (CommandLine.cpp:58-64).
 *  `fields` is [[key, value], …] or {key: value}; values already formatted (num, points). */
export function gradeSet(bind, filter, fields) {
  const list = Array.isArray(fields) ? fields : Object.entries(fields);
  let line = "set";
  for (const [k, v] of list) line += " " + q(`${bind}.${filter}.${k}=${v}`);
  return line;
}

/** `set <address>=<value>` with the assignment quoted as ONE token when it needs it - the spelling
 *  App.cpp uses for a value that may hold a path or a space (`set "<bind>.lut=<file>"`). */
export function setLine(address, value) { return "set " + q(`${address}=${value}`); }
