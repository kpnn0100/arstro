/*
 * Interstellar — kit/animatedrows.js: list insert / remove / reorder that TRAVELS (design rule §1).
 *
 * A port of app/widgets/AnimatedRows.h. A self-drawn list that draws model[i] at i × rowH breaks the
 * law the moment the model changes shape (a row added above shoves every row below in one frame; a
 * deleted render job vanishes). So each row is keyed by its model identity and carries its own eased
 * y and alpha:
 *   - a row that moves eases to its new y (motion::kSelectMs 200 ms EaseOutCubic);
 *   - a NEW row fades in at its slot (kCrossFadeMs) - except on the very first sync ("first placement
 *     sets": nowhere to travel from);
 *   - a REMOVED row stays as a ghost with its last data and fades out, then is dropped.
 * Reduced motion: every tween lands at once (core/motion.js).
 *
 *   const rows = new AnimatedRows(() => redraw())   onChange: every frame anything moves
 *   rows.sync([[key, data], …], rowH)               re-key to the model, display order
 *   rows.rows()  -> [{key, data, index (-1 = ghost), gone, y (live), alpha (live), yTarget}]
 *   rows.count() / rows.byIndex(i) / rows.moving()
 * Hit-test only rows with !gone.
 */
import { Tween, Ease } from "../core/motion.js";
import { tok } from "./tokens.js";

export class AnimatedRows {
  constructor(onChange) {
    this._rows = [];
    this._count = 0;
    this._synced = false;
    this._onChange = onChange || null;
    this._raf = 0;
  }
  _changed() {
    if (!this._onChange || this._raf) return;
    this._raf = requestAnimationFrame(() => {
      this._raf = 0;
      this._rows = this._rows.filter((r) => !(r.gone && !r._a.animating && r._a.value <= 0.001));
      this._onChange();
    });
  }
  sync(items, rowH) {
    const first = !this._synced;
    for (const r of this._rows) r.index = -1;
    items.forEach(([key, data], i) => {
      let row = this._rows.find((r) => r.key === key && !r.gone) || this._rows.find((r) => r.key === key);
      const y = i * rowH;
      if (!row) {
        row = { key, data, index: i, gone: false, yTarget: y,
                _y: new Tween(y, () => this._changed()), _a: new Tween(first ? 1 : 0, () => this._changed()) };
        Object.defineProperty(row, "y", { get: () => row._y.value });
        Object.defineProperty(row, "alpha", { get: () => row._a.value });
        this._rows.push(row);
        if (!first) row._a.to(1, tok.ms("motion-cross-fade"), Ease.EaseOutCubic);
      } else if (row.yTarget !== y) {
        row._y.to(y, tok.ms("motion-select"), Ease.EaseOutCubic);
      }
      if (row.gone) { row.gone = false; row._a.to(1, tok.ms("motion-cross-fade"), Ease.EaseOutCubic); }
      row.data = data;
      row.index = i;
      row.yTarget = y;
    });
    for (const r of this._rows) {
      if (r.index < 0 && !r.gone) { r.gone = true; r._a.to(0, tok.ms("motion-cross-fade"), Ease.EaseOutCubic); }
    }
    this._count = items.length;
    this._synced = true;
    this._changed();
  }
  rows() { return this._rows; }
  count() { return this._count; }
  byIndex(i) { return this._rows.find((r) => r.index === i) || null; }
  moving() { return this._rows.some((r) => r._y.animating || r._a.animating); }
}
