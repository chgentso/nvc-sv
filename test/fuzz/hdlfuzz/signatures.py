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


"""Failure signatures (plan section 6).

A signature identifies a kind of failure independently of the case: no
numbers, names, addresses or paths.  Scheme (slash separated, lower case):

    <level>-<what>/<where>/<kind>/<shape>

    L0-crash/<config>/<top two frames>          one config crashed
    L0-reject/<config>/<message, numbers and names removed>
    L0-timeout/<config>
    L0-unsupported/<config>/<message>
    L0-deltalimit/<config>
    L0-outcome/<cfg>-vs-<ref>/<class>-<class>   outcome classes differ
    L1-report/<cfg>-vs-<ref>/<kind>/<shape>     report text with time+delta
    L1-vcd/<cfg>-vs-<ref>/<kind>/<shape>        canonical VCD hash differs
    L2-report/<cfg>-vs-<ref>/<kind>/<shape>     cross tool, time but no delta
    L3-vcd/<cfg>-vs-<ref>/<kind>/<shape>        cross tool, per-name values

kind of a first difference: missing-event, extra-event, value, time-shift,
delta-shift, text-change, missing-record, extra-record, extra-signal.
shape: report messages reduced to a mask (digits and hex runs become #), VCD
differences to <width class>/<position> (scalar|vec|wide, t0|within-time|
later).  An optional hint (e.g. "comb-driven", from the generator's IR) is
appended by the campaign, which knows the IR.
"""

import re
from dataclasses import dataclass, field
from typing import List

_FRAME_SKIP = re.compile(
    r'signal|handler|sigaction|restore_rt|stacktrace|fatal_trace|^abort$|'
    r'^raise$|^gsignal$|^__|^_?show_|^backtrace|libc|ld-linux|^\?\?$', re.I)


# nvc configurations that run the same elaborated design through the island
# path (or a variant of it): one bug usually shows in several of them, so the
# campaign buckets them as one ("islands") and keeps the per-config detail.
ISLAND_CONFIGS = frozenset(("default", "nospec", "nofast", "module", "spec",
                            "specA", "stress", "threads", "jit", "interp",
                            "O0"))
GROUP = "islands"


def group_name(cfg):
    return GROUP if cfg in ISLAND_CONFIGS else cfg


@dataclass
class Difference:
    level: str                  # L0 | L1 | L2 | L3
    what: str                   # crash, reject, report, vcd, ...
    cfg: str
    ref: str = ""
    kind: str = ""
    shape: str = ""
    detail: str = ""            # first difference in words (not in signature)
    rules: List[str] = field(default_factory=list)
    hint: str = ""

    def signature(self, group=False):
        """group=True: island configurations share one name ("islands")."""
        cfg = group_name(self.cfg) if group else self.cfg
        where = cfg if not self.ref else "%s-vs-%s" % (cfg, self.ref)
        parts = ["%s-%s" % (self.level, self.what), where]
        if self.kind:
            parts.append(self.kind)
        if self.shape:
            parts.append(self.shape)
        if self.hint:
            parts.append(self.hint)
        return "/".join(parts).lower()


def mask(msg, limit=48):
    """Message with numbers, hex runs, quoted names and paths removed."""
    m = re.sub(r"'[^']*'|\"[^\"]*\"", "'_'", msg)
    m = re.sub(r"(?:/[\w.\-+]+)+(?::\d+)*", "<path>", m)
    m = re.sub(r"\b[\w.\-]+\.(?:vhd|vhdl|sv|v|c|h)(?::\d+)*\b", "<file>", m)
    m = re.sub(r"0x[0-9a-fA-F]+", "#", m)
    m = re.sub(r"\b[0-9a-fA-F]*\d[0-9a-fA-F]*\b", "#", m)
    m = re.sub(r"(#\s*)+", "# ", m)
    m = re.sub(r"\s+", " ", m).strip()
    return m[:limit]


_BITS = re.compile(r"\b(?=[01xzuwlh-]*[01xzuw])[01xzuwlh-]{2,}\b", re.I)
_SIGID = re.compile(r"\b[a-z]\w*?\d+(?:\.[a-z]\w*?\d+)+\b", re.I)


def mask_report(msg, limit=48):
    """mask() for the text of a monitor or sampler report: also hides
    std_logic strings (a run of 0/1/x/u/z/w/l/h/-) and generated signal
    paths (m3.s7), which otherwise split one bug into one bucket per
    value."""
    m = mask(msg, 1000)
    m = _SIGID.sub("#", m)
    m = _BITS.sub("#", m)
    m = re.sub(r"=\s*'_'", "=#", m)
    m = re.sub(r"(?<=\s)'_'$", "#", m)
    m = re.sub(r"#(?:\s+#)+", "#", m)
    return m[:limit]


def crash_frames(text, n=2):
    """Top n function names of a crash trace, no addresses.  nvc prints
    '[0x55d...] ../src/util.c:123 show_stacktrace' style lines, sanitizers
    '#3 0x... in function file:line'."""
    frames = []
    for line in text.splitlines():
        s = line.strip()
        name = None
        m = re.match(r'\[0x[0-9a-f]+\]\s+(?:\S+:\d+\s+)?(.*)$', s)
        if m:
            name = m.group(1)
        else:
            m = re.match(r'#\d+\s+0x[0-9a-f]+\s+in\s+(\S+)', s)
            if m:
                name = m.group(1)
        if not name:
            continue
        name = re.sub(r'\+0x[0-9a-f]+', '', name)
        name = re.sub(r'\(.*$', '', name).strip().split(" ")[0]
        name = name.split("/")[-1]
        if not name or _FRAME_SKIP.search(name):
            continue
        frames.append(name)
        if len(frames) == n:
            break
    return frames


def crash_shape(text):
    """Frames if there are any, else the failed assertion or message."""
    fr = crash_frames(text)
    if fr:
        return "+".join(fr)
    m = re.search(r"(?:Assertion|assertion)\s+'?(.*?)'?\s+failed", text)
    if m:
        return "assert:" + mask(m.group(1), 40)
    for line in text.splitlines():
        if re.search(r'internal error|Caught signal|Sanitizer|runtime error',
                     line, re.I):
            return mask(line, 48)
    return "unknown"


def first_message(text):
    """First error/fatal/sorry message of a run, masked."""
    for line in text.splitlines():
        m = re.match(r'\*\* (?:Error|Fatal|Failure): (.*)$', line)
        if m:
            msg = re.sub(r'^[\w./\-]+:\d+(?::\d+)?:\s*', '', m.group(1))
            return mask(msg)
        if re.search(r'\bsorry\b', line, re.I):
            return mask(line)
        m = re.match(r'.*?:\d+:\d+:\s*(?:error:\s*)?(.*)$', line)
        if m and "error" in line:
            return mask(m.group(1))
    return "unknown"


def single_difference(result):
    """Level-0 finding of one config on its own (crash, reject, timeout,
    unsupported, delta limit), or None."""
    o = result.outcome
    if o in ("ok", "failure"):
        return None
    d = Difference("L0", o, result.config)
    if o == "crash":
        d.shape = crash_shape(result.text)
    elif o in ("reject", "unsupported"):
        d.shape = first_message(result.text)
    d.detail = "%s in stage %s (rc=%d)" % (o, result.stage, result.rc)
    return d
