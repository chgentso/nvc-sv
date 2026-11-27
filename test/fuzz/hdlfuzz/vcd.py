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


"""VCD parsing and hashing for the comparison levels.

Hashing follows the perf lane's rule: the $date block is dropped, and inside
one #time block the changes are put in a canonical order (the order in which
different signals update inside one timestamp is not a difference).  The
strict hash (file order kept) is recorded as information.
"""

import hashlib
import re
from collections import defaultdict

REDUCE = str.maketrans({"u": "x", "w": "x", "-": "x", "l": "0", "h": "1"})


def reduce_value(v):
    """9-value to 4-value (0/1/x/z) for cross-tool compare: U W X - become x,
    L becomes 0, H becomes 1."""
    return v.lower().translate(REDUCE)


def _is_hdr_skip(line, state):
    """Drops the $date ... $end block; state is a one-element list."""
    if state[0]:
        if b"$end" in line:
            state[0] = False
        return True
    if line.startswith(b"$date"):
        state[0] = b"$end" not in line
        return True
    return False


def hashes(path):
    """(strict, canonical) sha256 of the date-stripped VCD."""
    strict, canon = hashlib.sha256(), hashlib.sha256()
    skip, hdr, block = [False], True, []

    def flush():
        def ident(l):
            t = l.split()
            return t[-1] if l[:1] in (b"b", b"B", b"r", b"R") and t \
                else l[1:].strip()
        for l in sorted(block, key=ident):
            canon.update(l)
        block.clear()

    with open(path, "rb") as f:
        for line in f:
            if hdr:
                if _is_hdr_skip(line, skip):
                    continue
                strict.update(line)
                canon.update(line)
                if line.startswith(b"$enddefinitions"):
                    hdr = False
                continue
            strict.update(line)
            if line.startswith(b"#"):
                flush()
                canon.update(line)
            else:
                block.append(line)
    flush()
    return strict.hexdigest(), canon.hexdigest()


class Vcd:
    """Parsed VCD: names (lower-cased, scopes joined with '.') map to the
    ordered list of (time, value) changes, glitches included."""

    def __init__(self):
        self.width = {}                       # name -> declared width
        self.events = {}                      # name -> [(time, value)]
        self.dup_ids = 0

    def names(self):
        return self.events.keys()


def parse(path):
    vcd = Vcd()
    codes = defaultdict(list)                 # id code -> [names]
    scope = []
    t = 0
    hdr = True
    with open(path, "r", errors="replace") as f:
        for raw in f:
            s = raw.strip()
            if not s:
                continue
            if hdr:
                if s.startswith("$scope"):
                    p = s.split()
                    scope.append(p[2] if len(p) > 2 else "")
                elif s.startswith("$upscope"):
                    if scope:
                        scope.pop()
                elif s.startswith("$var"):
                    p = s.split()
                    # $var type width code name [range] $end
                    if len(p) >= 6:
                        name = ".".join(scope + [p[4]]).lower()
                        if p[4].lower() in ("[", "]"):
                            continue
                        if name in vcd.events:
                            name = "%s#%d" % (name, len(vcd.events))
                        codes[p[3]].append(name)
                        vcd.width[name] = int(p[2]) if p[2].isdigit() else 1
                        vcd.events[name] = []
                elif s.startswith("$enddefinitions"):
                    hdr = False
                continue
            c = s[0]
            if c == "#":
                try:
                    t = int(s[1:])
                except ValueError:
                    pass
            elif c in "bBrR":
                parts = s.split()
                if len(parts) != 2:
                    continue
                for n in codes.get(parts[1], ()):
                    vcd.events[n].append((t, _extend(parts[0], vcd.width[n])))
            elif c in "01xXzZuUwWhHlL-":
                for n in codes.get(s[1:], ()):
                    vcd.events[n].append((t, c.lower()))
            # $dumpvars, $end and friends are ignored
    return vcd


def _extend(tok, width):
    """b101 -> '101' left-extended to width with 0 (or x/z when the first
    digit is x/z); real values keep their text."""
    if tok[0] in "rR":
        return tok.lower()
    v = tok[1:].lower()
    if len(v) < width and v:
        pad = v[0] if v[0] in "xz" else "0"
        v = pad * (width - len(v)) + v
    return v


def final_series(events, reduce=False):
    """[(time, last value at that time)], consecutive equal values dropped."""
    out = []
    for t, v in events:
        if reduce:
            v = reduce_value(v)
        if out and out[-1][0] == t:
            out[-1] = (t, v)
        else:
            out.append((t, v))
    ded = []
    for t, v in out:
        if not ded or ded[-1][1] != v:
            ded.append((t, v))
    return ded
