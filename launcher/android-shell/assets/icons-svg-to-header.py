#!/usr/bin/env python3
"""
arstro-android-shell icon codegen (M2.3).

Turns each SVG in a source dir into a static C++ IconPath table (theme/icons/IconTypes.h),
flattening every segment to Move/Line/Cubic/Close ops — arcs and quadratics become cubics,
because Artboard's render HAL has only cubic beziers (no arc/quad-preserving primitive here;
quadTo exists but we normalise to cubic for one uniform op table).

Supports the common SVG path grammar: M/m L/l H/h V/v C/c S/s Q/q T/t A/a Z/z. Stroke vs fill
is inferred from `fill="none"` on the <svg>/<path> (line icon -> stroke, else solid -> fill).
The coordinate system (viewSize) comes from the viewBox (default 24).

Usage: icons-svg-to-header.py <src-dir> <out-header>
Deterministic: no timestamps, sorted by filename, so the generated header is reproducible.
"""
import sys
import os
import re
import math
import glob

# ---- tiny SVG path tokenizer/parser -> list of ('M'|'L'|'C'|'Z', coords) absolute ops ----

_NUM = re.compile(r'[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?')


def tokenize(d):
    toks = []
    i, n = 0, len(d)
    while i < n:
        c = d[i]
        if c in 'MmLlHhVvCcSsQqTtAaZz':
            toks.append(c)
            i += 1
        elif c in ' ,\t\r\n':
            i += 1
        else:
            m = _NUM.match(d, i)
            if not m:
                i += 1
                continue
            toks.append(float(m.group()))
            i = m.end()
    return toks


def quad_to_cubic(p0, ctrl, p1):
    c1 = (p0[0] + 2.0 / 3.0 * (ctrl[0] - p0[0]), p0[1] + 2.0 / 3.0 * (ctrl[1] - p0[1]))
    c2 = (p1[0] + 2.0 / 3.0 * (ctrl[0] - p1[0]), p1[1] + 2.0 / 3.0 * (ctrl[1] - p1[1]))
    return c1, c2


def arc_to_cubics(p0, rx, ry, phi_deg, large, sweep, p1):
    """Endpoint-parameterised elliptical arc -> list of cubic segments (each c1,c2,end)."""
    if rx == 0 or ry == 0 or p0 == p1:
        return [(p0, p1, p1)]  # degenerate -> straight line as a cubic
    phi = math.radians(phi_deg)
    cosp, sinp = math.cos(phi), math.sin(phi)
    # step 1: compute (x1',y1')
    dx, dy = (p0[0] - p1[0]) / 2.0, (p0[1] - p1[1]) / 2.0
    x1p = cosp * dx + sinp * dy
    y1p = -sinp * dx + cosp * dy
    rx, ry = abs(rx), abs(ry)
    # correct radii
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx *= s
        ry *= s
    # step 2: compute center'
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den != 0 else 0.0
    if large == sweep:
        co = -co
    cxp = co * rx * y1p / ry
    cyp = -co * ry * x1p / rx
    # step 3: center in original coords
    cx = cosp * cxp - sinp * cyp + (p0[0] + p1[0]) / 2.0
    cy = sinp * cxp + cosp * cyp + (p0[1] + p1[1]) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        ln = math.hypot(ux, uy) * math.hypot(vx, vy)
        a = math.acos(max(-1.0, min(1.0, dot / ln))) if ln else 0.0
        if ux * vy - uy * vx < 0:
            a = -a
        return a

    theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dtheta > 0:
        dtheta -= 2 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2 * math.pi

    # split into <= 90deg segments, each an exact-ish cubic
    nseg = max(1, int(math.ceil(abs(dtheta) / (math.pi / 2.0))))
    delta = dtheta / nseg
    t = 4.0 / 3.0 * math.tan(delta / 4.0)
    out = []
    start = theta1
    for _ in range(nseg):
        a1 = start
        a2 = start + delta

        def pt(a):
            x = cx + rx * math.cos(a) * cosp - ry * math.sin(a) * sinp
            y = cy + rx * math.cos(a) * sinp + ry * math.sin(a) * cosp
            return (x, y)

        p_start = pt(a1)
        p_end = pt(a2)
        # tangents
        dx1 = -rx * math.sin(a1) * cosp - ry * math.cos(a1) * sinp
        dy1 = -rx * math.sin(a1) * sinp + ry * math.cos(a1) * cosp
        dx2 = -rx * math.sin(a2) * cosp - ry * math.cos(a2) * sinp
        dy2 = -rx * math.sin(a2) * sinp + ry * math.cos(a2) * cosp
        c1 = (p_start[0] + t * dx1, p_start[1] + t * dy1)
        c2 = (p_end[0] - t * dx2, p_end[1] - t * dy2)
        out.append((c1, c2, p_end))
        start = a2
    return out


def parse_path(d):
    """Return list of ops: ('M',x,y) ('L',x,y) ('C',c1x,c1y,c2x,c2y,x,y) ('Z',)."""
    toks = tokenize(d)
    ops = []
    i = 0
    cur = (0.0, 0.0)
    start = (0.0, 0.0)
    prev_cmd = None
    prev_c2 = None   # for S: reflected control
    prev_q = None    # for T: reflected quad control
    cmd = None

    def num():
        nonlocal i
        v = toks[i]
        i += 1
        return v

    while i < len(toks):
        if isinstance(toks[i], str):
            cmd = toks[i]
            i += 1
        # implicit repeat: keep cmd (M becomes L on repeat)
        rel = cmd.islower()
        C = cmd.upper()
        if C == 'M':
            x, y = num(), num()
            if rel:
                x, y = cur[0] + x, cur[1] + y
            cur = (x, y)
            start = cur
            ops.append(('M', x, y))
            cmd = 'l' if rel else 'L'  # subsequent implicit -> lineto
        elif C == 'L':
            x, y = num(), num()
            if rel:
                x, y = cur[0] + x, cur[1] + y
            cur = (x, y)
            ops.append(('L', x, y))
        elif C == 'H':
            x = num()
            if rel:
                x = cur[0] + x
            cur = (x, cur[1])
            ops.append(('L', cur[0], cur[1]))
        elif C == 'V':
            y = num()
            if rel:
                y = cur[1] + y
            cur = (cur[0], y)
            ops.append(('L', cur[0], cur[1]))
        elif C == 'C':
            c1x, c1y, c2x, c2y, x, y = num(), num(), num(), num(), num(), num()
            if rel:
                c1x, c1y = cur[0] + c1x, cur[1] + c1y
                c2x, c2y = cur[0] + c2x, cur[1] + c2y
                x, y = cur[0] + x, cur[1] + y
            ops.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_c2 = (c2x, c2y)
            cur = (x, y)
        elif C == 'S':
            c2x, c2y, x, y = num(), num(), num(), num()
            if rel:
                c2x, c2y = cur[0] + c2x, cur[1] + c2y
                x, y = cur[0] + x, cur[1] + y
            if prev_cmd in ('C', 'S') and prev_c2:
                c1x, c1y = 2 * cur[0] - prev_c2[0], 2 * cur[1] - prev_c2[1]
            else:
                c1x, c1y = cur
            ops.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_c2 = (c2x, c2y)
            cur = (x, y)
        elif C == 'Q':
            qx, qy, x, y = num(), num(), num(), num()
            if rel:
                qx, qy = cur[0] + qx, cur[1] + qy
                x, y = cur[0] + x, cur[1] + y
            c1, c2 = quad_to_cubic(cur, (qx, qy), (x, y))
            ops.append(('C', c1[0], c1[1], c2[0], c2[1], x, y))
            prev_q = (qx, qy)
            cur = (x, y)
        elif C == 'T':
            x, y = num(), num()
            if rel:
                x, y = cur[0] + x, cur[1] + y
            if prev_cmd in ('Q', 'T') and prev_q:
                qx, qy = 2 * cur[0] - prev_q[0], 2 * cur[1] - prev_q[1]
            else:
                qx, qy = cur
            c1, c2 = quad_to_cubic(cur, (qx, qy), (x, y))
            ops.append(('C', c1[0], c1[1], c2[0], c2[1], x, y))
            prev_q = (qx, qy)
            cur = (x, y)
        elif C == 'A':
            rx, ry, rot, large, sweep, x, y = num(), num(), num(), num(), num(), num(), num()
            if rel:
                x, y = cur[0] + x, cur[1] + y
            for (c1, c2, end) in arc_to_cubics(cur, rx, ry, rot, int(large), int(sweep), (x, y)):
                ops.append(('C', c1[0], c1[1], c2[0], c2[1], end[0], end[1]))
            cur = (x, y)
        elif C == 'Z':
            ops.append(('Z',))
            cur = start
        prev_cmd = C
    return ops


def read_svg(path):
    text = open(path, 'r').read()
    vb = re.search(r'viewBox\s*=\s*"([^"]+)"', text)
    view = 24.0
    if vb:
        parts = vb.group(1).split()
        if len(parts) == 4:
            view = max(float(parts[2]), float(parts[3]))
    stroke = ('fill="none"' in text.replace(' ', '') or "fill='none'" in text.replace(' ', ''))
    ds = re.findall(r'\bd\s*=\s*"([^"]+)"', text)
    ops = []
    for d in ds:
        ops.extend(parse_path(d))
    return view, stroke, ops


def cpp_name(fname):
    base = os.path.splitext(os.path.basename(fname))[0]
    return 'k' + ''.join(w.capitalize() for w in re.split(r'[_\-]', base))


def emit(src_dir, out_header):
    svgs = sorted(glob.glob(os.path.join(src_dir, '*.svg')))
    lines = []
    lines.append('// GENERATED by assets/icons-svg-to-header.py — do not edit. Regenerated by CMake.')
    lines.append('#pragma once')
    lines.append('#include "theme/icons/IconTypes.h"')
    lines.append('')
    lines.append('namespace arstro { namespace androidshell { namespace icons {')
    lines.append('')
    names = []
    for svg in svgs:
        view, stroke, ops = read_svg(svg)
        name = cpp_name(svg)
        names.append(name)
        arr = name + '_ops'
        lines.append('inline const IconOp %s[] = {' % arr)
        for op in ops:
            if op[0] == 'M':
                lines.append('    {IconOp::Move, {%ff, %ff}},' % (op[1], op[2]))
            elif op[0] == 'L':
                lines.append('    {IconOp::Line, {%ff, %ff}},' % (op[1], op[2]))
            elif op[0] == 'C':
                lines.append('    {IconOp::Cubic, {%ff, %ff, %ff, %ff, %ff, %ff}},' %
                             (op[1], op[2], op[3], op[4], op[5], op[6]))
            elif op[0] == 'Z':
                lines.append('    {IconOp::Close, {}},')
        lines.append('};')
        lines.append('inline const IconPath %s{%s, %d, %ff, %s};' %
                     (name, arr, len(ops), view, 'true' if stroke else 'false'))
        lines.append('')
    lines.append('}}} // namespace')
    os.makedirs(os.path.dirname(out_header), exist_ok=True)
    with open(out_header, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return names


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.stderr.write('usage: icons-svg-to-header.py <src-dir> <out-header>\n')
        sys.exit(2)
    generated = emit(sys.argv[1], sys.argv[2])
    sys.stderr.write('arstro-android-shell: generated %d icons -> %s\n' %
                     (len(generated), sys.argv[2]))
