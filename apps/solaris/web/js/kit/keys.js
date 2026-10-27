/*
 * Solaris — kit/keys.js: a key press as the App sees it (app/KeyInput.h, core/service/KeyCombo.h, Keymap.h; R-SET-4).
 *
 *   keyCode(e)               KeyInput::keyCodeOfKeysym for a KeyboardEvent: a letter or digit is its upper-case ASCII
 *                            (the UNSHIFTED key — Shift+1 is "Shift+1", never "!": e.code for digits and, off a Latin
 *                            layout, letters), F1…F12 = 112…123, every other key its Windows virtual-key number; 0 for a
 *                            key no combo can name (a modifier alone, a media key).
 *   comboText(code, ctrl, alt, shift)   KeyCombo::comboText — "Ctrl+Alt+Shift+<key>", or "".
 *   comboOf(e)               comboText of a KeyboardEvent. Ctrl is Ctrl — or ⌘ on a Mac (metaKey): the same combo.
 *   keyTypes(e)              KeyInput::keyTypes: the press also TYPES (a printable character, no Ctrl / Alt).
 *   keyGroup(area)           Keymap::keyGroup: "roll" for the piano roll's area, else "song".
 *   keyActionFor(m, combo, group)   App::keyActionFor: the action a combo means in the model's keymap
 *                            (m.settings.keys: [{id, area, label, keys, defaults}]), or "".
 *   keyHint(m, action)       App::keyHint: "   (Ctrl+Z)" — the keymap's first key for an action — or "".
 */

const NAMED = [[8, "Backspace"], [9, "Tab"], [13, "Enter"], [27, "Escape"], [32, "Space"], [33, "PageUp"], [34, "PageDown"], [35, "End"],
               [36, "Home"], [37, "Left"], [38, "Up"], [39, "Right"], [40, "Down"], [45, "Insert"], [46, "Delete"], [186, "Semicolon"],
               [187, "Equal"], [188, "Comma"], [189, "Minus"], [190, "Period"], [191, "Slash"], [192, "Backquote"], [219, "BracketLeft"],
               [220, "Backslash"], [221, "BracketRight"], [222, "Quote"]];
const NAME_OF = new Map(NAMED);
export const KEY_F1 = 112;

// KeyboardEvent.key of the keys KeyInput.h names by keysym
const BY_KEY = { Backspace: 8, Tab: 9, Enter: 13, Escape: 27, " ": 32, PageUp: 33, PageDown: 34, End: 35, Home: 36, ArrowLeft: 37,
                 ArrowUp: 38, ArrowRight: 39, ArrowDown: 40, Insert: 45, Delete: 46 };
// KeyboardEvent.code of the punctuation keys (the key, not what it types with Shift)
const BY_CODE = { Semicolon: 186, Equal: 187, Comma: 188, Minus: 189, Period: 190, Slash: 191, Backquote: 192, BracketLeft: 219,
                  Backslash: 220, BracketRight: 221, Quote: 222, NumpadEnter: 13, NumpadDecimal: 46, Space: 32 };

export function keyCode(e) {
  const k = e.key || "", c = e.code || "";
  if (/^Digit[0-9]$/.test(c)) return c.charCodeAt(5);
  if (k.length === 1 && /[a-z]/i.test(k)) return k.toUpperCase().charCodeAt(0);
  if (/^Key[A-Z]$/.test(c)) return c.charCodeAt(3);
  if (k.length === 1 && /[0-9]/.test(k)) return k.charCodeAt(0);
  const f = /^F([1-9]|1[0-2])$/.exec(k);
  if (f) return KEY_F1 + parseInt(f[1], 10) - 1;
  if (k in BY_KEY) return BY_KEY[k];
  if (c in BY_CODE) return BY_CODE[c];
  return 0;
}

export function keyNameOf(code) {
  if ((code >= 65 && code <= 90) || (code >= 48 && code <= 57)) return String.fromCharCode(code);
  if (code >= KEY_F1 && code < KEY_F1 + 12) return "F" + (code - KEY_F1 + 1);
  return NAME_OF.get(code) || "";
}

export function comboText(code, ctrl, alt, shift) {
  const k = keyNameOf(code);
  if (!k) return "";
  return (ctrl ? "Ctrl+" : "") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + k;
}

export const comboOf = (e) => comboText(keyCode(e), e.ctrlKey || e.metaKey, e.altKey, e.shiftKey);
export const keyTypes = (e) => !(e.ctrlKey || e.metaKey) && !e.altKey && e.key && [...e.key].length === 1 && e.key.codePointAt(0) >= 0x20 && e.key !== "\x7f";
export const keyGroup = (area) => (area === "Piano roll" ? "roll" : "song");

export function keyActionFor(m, combo, group) {
  if (!m || !combo) return "";
  for (const k of (m.settings && m.settings.keys) || []) if (keyGroup(k.area) === group && (k.keys || []).includes(combo)) return k.id;
  return "";
}

export function keyHint(m, action) {
  for (const k of (m && m.settings && m.settings.keys) || []) if (k.id === action && k.keys && k.keys.length) return "   (" + k.keys[0] + ")";
  return "";
}
