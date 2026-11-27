#
#  Copyright (C) 2026  Nick Gasson
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
#


"""Comparison levels L0, L1, L3 (plan section 5.1) and the verdict of a case.

    L0  outcome class (ok, failure, crash, timeout, ...)
    L1  nvc text with time+delta (N1: records inside one time+delta group are
        sorted, unless the build orders reports itself) and the VCD (canonical
        hash: order inside one #time block is free)
    L2  cross tool report records, time but no delta (nvc vs GHDL)
    L3  cross tool VCD: per name, last value at every time, 9 values reduced

Every normalisation is a named rule recorded on the Difference.
"""

import re
from collections import Counter
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from . import vcd as vcdmod
from .signatures import Difference, mask, single_difference

RULE_N1 = "N1-sort-within-delta"
RULE_VCD_ORDER = "vcd-order-within-time"
RULE_9VAL = "x-9val-reduce"
RULE_NAMES = "x-common-names-only"
RULE_METAVALUE = "x-drop-metavalue-warnings"
RULE_PROC = "x-drop-process-line"
RULE_FINISH = "x-drop-finish-note"
RULE_NODELTA = "x-reports-without-delta"
RULE_CUT = "x-cut-at-early-end"

UNIT_FS = {"fs": 1, "ps": 10**3, "ns": 10**6, "us": 10**9, "ms": 10**12,
           "sec": 10**15, "s": 10**15}

NVC_REC = re.compile(r'^\*\* (\w+): (\d+(?:\.\d+)?)\s?([a-z]+)(?:\+(\d+))?: '
                     r'(.*)$')
GHDL_REC = re.compile(r'^(?:\S+?:\d+:\d+:)?@(\d+(?:\.\d+)?)\s?([a-z]+)'
                      r'(?:\+\+(\d+))?:\((?:assertion |report )?(\w+)\): '
                      r'(.*)$')


@dataclass(frozen=True)
class Rec:
    sev: str
    time: str            # as printed, e.g. "10ns"
    delta: int
    msg: str             # first line plus continuation lines


def time_fs(num, unit):
    return int(float(num) * UNIT_FS.get(unit, 1))


PROC_LINE = re.compile(r'^\s+Process :')


def parse_nvc(text, drop_process=False):
    """Items of nvc output in order: Rec for '** Sev: time+delta: msg' blocks
    (with indented continuation lines), plain strings for the rest."""
    items, cur = [], None
    for line in text.splitlines():
        m = NVC_REC.match(line)
        if m:
            if cur:
                items.append(_mk(cur))
            cur = [m.group(1).lower(), m.group(2) + m.group(3),
                   int(m.group(4) or 0), [m.group(5)]]
        elif cur is not None and line[:1] in (" ", "\t"):
            # a fused process is named differently in the trace
            if not (drop_process and PROC_LINE.match(line)):
                cur[3].append(line.rstrip())
        else:
            if cur:
                items.append(_mk(cur))
                cur = None
            if line.strip():
                items.append(line.rstrip())
    if cur:
        items.append(_mk(cur))
    return items


def _mk(c):
    return Rec(c[0], c[1], c[2], "\n".join(c[3]))


def n1_sort(items):
    """Sort Recs inside runs of equal (time, delta); other lines break runs."""
    out, run, key = [], [], None

    def flush():
        out.extend(sorted(run, key=lambda r: (r.sev, r.msg)))
        run.clear()

    for it in items:
        if isinstance(it, Rec):
            k = (it.time, it.delta)
            if k != key:
                flush()
                key = k
            run.append(it)
        else:
            flush()
            key = None
            out.append(it)
    flush()
    return out


def parse_ghdl(text):
    """GHDL report lines as (time_fs, severity, message)."""
    out = []
    for line in text.splitlines():
        m = GHDL_REC.match(line.strip())
        if m:
            out.append((time_fs(m.group(1), m.group(2)), m.group(4).lower(),
                        m.group(5)))
    return out


def nvc_records(items):
    return [(time_fs(*re.match(r'([\d.]+)([a-z]+)', r.time).groups()),
             r.sev, r.msg.split("\n")[0])
            for r in items if isinstance(r, Rec)]


# ------------------------------------------------------------ L1 reports

def _item_shape(it):
    if isinstance(it, Rec):
        return "%s:%s" % (it.sev, mask(it.msg.split("\n")[0]))
    return "plain:" + mask(it)


def diff_items(a, b):
    """First difference between item lists a (config) and b (reference):
    (kind, shape, detail) or None."""
    n = min(len(a), len(b))
    i = next((k for k in range(n) if a[k] != b[k]), None)
    if i is None:
        if len(a) == len(b):
            return None
        if len(a) > len(b):
            return ("extra-record", _item_shape(a[n]),
                    "config has extra record: %s" % _short(a[n]))
        return ("missing-record", _item_shape(b[n]),
                "config lacks record: %s" % _short(b[n]))
    x, y = a[i], b[i]
    if a[i + 1:] == b[i:]:
        return ("extra-record", _item_shape(x),
                "extra record #%d: %s" % (i, _short(x)))
    if a[i:] == b[i + 1:]:
        return ("missing-record", _item_shape(y),
                "missing record #%d: %s" % (i, _short(y)))
    if isinstance(x, Rec) and isinstance(y, Rec):
        if x.sev == y.sev and x.msg == y.msg:
            if x.time == y.time:
                kind = "delta-shift"
            else:
                kind = "time-shift"
        else:
            kind = "text-change"
        return (kind, _item_shape(y),
                "record #%d: config %s vs ref %s" % (i, _short(x), _short(y)))
    return ("text-change", _item_shape(y),
            "record #%d: config %s vs ref %s" % (i, _short(x), _short(y)))


def _short(it):
    if isinstance(it, Rec):
        return "%s+%d %s %s" % (it.time, it.delta, it.sev,
                                it.msg.split("\n")[0][:60])
    return it[:60]


def compare_reports(res_a, res_b, stable=False):
    """L1 report text of config a against reference b."""
    a = parse_nvc(res_a.text, drop_process=True)
    b = parse_nvc(res_b.text, drop_process=True)
    rules = [RULE_PROC]
    if not stable:
        a, b = n1_sort(a), n1_sort(b)
        rules.append(RULE_N1)
    d = diff_items(a, b)
    if d is None:
        return None
    return Difference("L1", "report", res_a.config, res_b.config,
                      d[0], d[1], d[2], rules)


# -------------------------------------------------------------- VCD diffs

def _width_class(w):
    return "scalar" if w <= 1 else ("vec" if w <= 64 else "wide")


def _classify_seq(a, b):
    """Kind of the first difference between event lists a and b, and the
    index of it."""
    n = min(len(a), len(b))
    i = next((k for k in range(n) if a[k] != b[k]), n)
    if len(a) < len(b):
        kind = "missing-event"
    elif len(a) > len(b):
        kind = "extra-event"
    elif a[i][1] == b[i][1]:
        kind = "time-shift"
    else:
        kind = "value"
    return kind, i


def _position(a, b, i, kind):
    ta = a[i][0] if i < len(a) else None
    tb = b[i][0] if i < len(b) else None
    t = min(x for x in (ta, tb) if x is not None)
    if t == 0:
        return "t0", t
    if kind in ("missing-event", "extra-event") and \
       {x[0] for x in a} == {x[0] for x in b}:
        return "within-time", t        # glitch count differs, same timestamps
    return "later", t


def diff_event_maps(ea, eb, widths, level, cfg, ref, series=None):
    """First difference between two name -> events maps (common names only
    when series is given, all names otherwise)."""
    names = sorted(set(ea) | set(eb))
    best = None
    for n in names:
        if n not in ea or n not in eb:
            if series is None:
                cand = (-1, n, "extra-signal" if n in ea else "missing-signal",
                        "", "signal %s only in %s" % (n, "config" if n in ea
                                                      else "ref"))
                if best is None or cand[:2] < best[:2]:
                    best = cand
            continue
        a, b = ea[n], eb[n]
        if a == b:
            continue
        kind, i = _classify_seq(a, b)
        pos, t = _position(a, b, i, kind)
        shape = "%s/%s" % (_width_class(widths.get(n, 1)), pos)
        det = "%s %s at %d (event %d): config %s vs ref %s" % (
            kind, n, t, i, a[i] if i < len(a) else "-",
            b[i] if i < len(b) else "-")
        cand = (t, n, kind, shape, det)
        if best is None or cand[:2] < best[:2]:
            best = cand
    if best is None:
        return None
    return Difference(level, "vcd", cfg, ref, best[2], best[3], best[4])


def compare_vcd_nvc(res_a, res_b):
    """L1 waveform of config a against reference b (both nvc)."""
    ha, hb = vcdmod.hashes(res_a.wave), vcdmod.hashes(res_b.wave)
    if ha[1] == hb[1]:
        return None, ha[0] == hb[0]
    va, vb = vcdmod.parse(res_a.wave), vcdmod.parse(res_b.wave)
    d = diff_event_maps(va.events, vb.events, vb.width, "L1", res_a.config,
                        res_b.config)
    if d is None:
        d = Difference("L1", "vcd", res_a.config, res_b.config, "order",
                       "other", "canonical hash differs, per-name events equal")
    d.rules.append(RULE_VCD_ORDER)
    return d, False


def _strip_top(names):
    return {n.split(".", 1)[1] if "." in n else n: n for n in names}


def common_series(va, vb):
    """name -> reduced final series for names in both VCDs (full names; if
    there is no overlap, with the top scope stripped)."""
    ka, kb = set(va.events), set(vb.events)
    if not (ka & kb):
        sa, sb = _strip_top(ka), _strip_top(kb)
        keys = set(sa) & set(sb)
        ma = {k: va.events[sa[k]] for k in keys}
        mb = {k: vb.events[sb[k]] for k in keys}
        wb = {k: vb.width.get(sb[k], 1) for k in keys}
    else:
        keys = ka & kb
        ma = {k: va.events[k] for k in keys}
        mb = {k: vb.events[k] for k in keys}
        wb = {k: vb.width.get(k, 1) for k in keys}
    fa = {k: vcdmod.final_series(v, True) for k, v in ma.items()}
    fb = {k: vcdmod.final_series(v, True) for k, v in mb.items()}
    return fa, fb, wb


def compare_vcd_cross(res_a, res_b, cut=False):
    """L3: config a (nvc) against b (other tool); returns (Difference|None,
    number of common names).  cut=True: a run ended early (fatal or failure)
    and each tool dumps a different part of the last time step, so everything
    from the earlier of the two end times on is left out of both."""
    va, vb = vcdmod.parse(res_a.wave), vcdmod.parse(res_b.wave)
    rules = []
    if cut:
        t = min(va.end_time, vb.end_time)
        va.events = vcdmod.cut_before(va.events, t)
        vb.events = vcdmod.cut_before(vb.events, t)
        rules.append(RULE_CUT)
    fa, fb, wb = common_series(va, vb)
    d = diff_event_maps(fa, fb, wb, "L3", res_a.config, res_b.config,
                        series=True)
    if d:
        d.rules += [RULE_9VAL, RULE_NAMES] + rules
    return d, len(fa)


# --------------------------------------------------------- L2 cross tool

def _drop_noise(recs):
    """Records that exist in one tool only: IEEE metavalue warnings (other
    wording) and nvc's note for std.env.finish."""
    return [r for r in recs if "metavalue" not in r[2].lower()
            and "ieee" not in r[2].lower() and r[2] != "FINISH called"]


def compare_reports_cross(res_nvc, res_other):
    """L2: report records (time, severity, text) of an nvc config against
    GHDL; compared as a multiset per time (no delta on the other side)."""
    a = _drop_noise(nvc_records(parse_nvc(res_nvc.text)))
    b = _drop_noise(parse_ghdl(res_other.text))
    times = sorted({r[0] for r in a} | {r[0] for r in b})
    for t in times:
        ca = Counter(r for r in a if r[0] == t)
        cb = Counter(r for r in b if r[0] == t)
        if ca == cb:
            continue
        only_a, only_b = list((ca - cb).elements()), list((cb - ca).elements())
        if only_a and only_b:
            kind, r = "text-change", only_b[0]
        elif only_a:
            kind, r = "extra-record", only_a[0]
        else:
            kind, r = "missing-record", only_b[0]
        return Difference("L2", "report", res_nvc.config, res_other.config,
                          kind, "%s:%s" % (r[1], mask(r[2])),
                          "at %d fs: nvc-only %s, other-only %s" % (
                              t, only_a[:1], only_b[:1]),
                          [RULE_NODELTA, RULE_METAVALUE, RULE_FINISH])
    return None


# ----------------------------------------------------------------- verdict

@dataclass
class Verdict:
    diffs: List[Difference] = field(default_factory=list)
    notes: List[str] = field(default_factory=list)
    blame: str = ""
    info: Dict[str, object] = field(default_factory=dict)

    def signatures(self, group=False):
        """Distinct signatures in order of discovery; group=True merges the
        island configurations into one name."""
        seen, out = set(), []
        for d in self.diffs:
            s = d.signature(group)
            if s not in seen:
                seen.add(s)
                out.append(s)
        return out

    def configs(self):
        """Grouped signature -> sorted list of the configs that show it."""
        m = {}
        for d in self.diffs:
            m.setdefault(d.signature(True), set()).add(d.cfg)
        return {s: sorted(c) for s, c in m.items()}

    def primary(self, group=False):
        """Signature of the first difference by priority (L0 first)."""
        order = {"L0": 0, "L1": 1, "L2": 2, "L3": 3}
        ds = sorted(self.diffs, key=lambda d: order.get(d.level, 9))
        return ds[0].signature(group) if ds else ""


def compare_case(results, ref="ref", levels=("L0", "L1", "L3"),
                 report_stable=False):
    """Compare the config results of one case against the reference config
    (nvc configs: L0, L1; GHDL: L0, L2, L3)."""
    v = Verdict()
    r = results.get(ref)
    if r is None:
        v.notes.append("no reference result")
        return v
    # level 0 findings of single configs (class C: legal code must not
    # crash, hang or be rejected); a tool that merely cannot take the case
    # (GHDL) is a note
    for name, res in results.items():
        if not res.available:
            v.notes.append("%s unavailable: %s" % (name, res.outcome))
            continue
        d = single_difference(res)
        if d:
            v.diffs.append(d)
    for name, res in results.items():
        if name == ref or not res.available:
            continue
        if res.config == "ghdl":
            _cross(v, r, res, levels)
            continue
        _nvc_pair(v, r, res, levels, report_stable)
    _blame(v, results, ref)
    return v


def _bad(res):
    return res.outcome in ("crash", "timeout", "unsupported", "reject",
                           "deltalimit", "incomplete")


def _nvc_pair(v, ref, res, levels, stable):
    if _bad(ref) or _bad(res):
        return
    if "L0" in levels and res.outcome != ref.outcome:
        v.diffs.append(Difference(
            "L0", "outcome", res.config, ref.config, "",
            "%s-%s" % (res.outcome, ref.outcome),
            "outcome %s vs ref %s" % (res.outcome, ref.outcome)))
        return
    if "L1" in levels:
        d = compare_reports(res, ref, stable)
        if d:
            v.diffs.append(d)
        if res.wave and ref.wave:
            d, strict_eq = compare_vcd_nvc(res, ref)
            v.info["%s.vcd_strict_equal" % res.config] = strict_eq
            if d:
                v.diffs.append(d)
        else:
            v.notes.append("%s: no waveform to compare" % res.config)


def _ended_early(a, b):
    """True if either run stopped with a failure instead of the end marker."""
    return a.outcome == "failure" or b.outcome == "failure"


def _cross(v, ref, g, levels):
    if _bad(ref) or _bad(g):
        return
    if "L0" in levels and g.outcome != ref.outcome:
        v.diffs.append(Difference(
            "L0", "outcome", g.config, ref.config, "",
            "%s-%s" % (g.outcome, ref.outcome),
            "outcome %s vs ref %s" % (g.outcome, ref.outcome)))
        return
    if "L2" in levels or "L3" in levels:
        d = compare_reports_cross(ref, g)
        if d:
            d.cfg, d.ref = g.config, ref.config
            v.diffs.append(d)
    if "L3" in levels and ref.wave and g.wave:
        d, ncommon = compare_vcd_cross(ref, g, _ended_early(ref, g))
        v.info["ghdl.common_names"] = ncommon
        if ncommon == 0:
            v.notes.append("L3: no common names with ghdl (rule gap)")
        if d:
            d.cfg, d.ref = g.config, ref.config
            v.diffs.append(d)


def _blame(v, results, ref):
    """Majority rule: islands differ from ref; whom does GHDL side with?"""
    isl = [d for d in v.diffs if d.ref == ref and d.cfg != "ghdl"
           and d.level in ("L1", "L0")]
    g = results.get("ghdl")
    if not isl:
        return
    if g is None or not g.available or not g.wave:
        v.blame = "islands"
        return
    other = results.get(isl[0].cfg)
    if other is None or not other.wave:
        v.blame = "islands"
        return
    d_ref, _ = compare_vcd_cross(results[ref], g,
                                 _ended_early(results[ref], g))
    d_oth, _ = compare_vcd_cross(other, g, _ended_early(other, g))
    if d_ref is None and d_oth is not None:
        v.blame = "islands"
    elif d_ref is not None and d_oth is None:
        v.blame = "reference-path"
    elif d_ref is None and d_oth is None:
        v.blame = "no-witness"         # L3 cannot see delta-level differences
    else:
        v.blame = "unknown"
