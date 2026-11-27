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

"""Adapter between a case directory on disk and the runner.

Native reader of the generator lane's case format (FORMAT.md, format 1): the
directory holds case.json ({"format": 1, "run": {...}, "ir": {...}}) and the
sources named in run.files.  The runner needs only the run block:

    run.lang, run.std       "vhdl", "2008"
    run.files               analysis order
    run.top                 testbench entity (fz_tb)
    run.stop_time_ns        guard for --stop-time
    run.end_marker          report text that proves the run completed
    run.observe.vcd         false: run without a wave dump

Hand-written cases use the same layout with "ir" left out.  A directory
without case.json is read by file extension (sources sorted by name, top
"tb"), which is only a convenience for experiments.  Everything that depends
on the layout lives in load_case().
"""

import json
import os
from dataclasses import dataclass, field
from typing import List

VHDL_EXT = (".vhd", ".vhdl")
SV_EXT = (".sv", ".v")
SUPPORTED_FORMAT = 1


@dataclass
class Case:
    dir: str
    name: str
    lang: str = "vhdl"
    std: str = "2008"
    top: str = "tb"
    files: List[str] = field(default_factory=list)   # absolute paths
    stop_time: str = "10us"
    waves: bool = True
    end_marker: str = ""
    meta: dict = field(default_factory=dict)

    def source_size(self):
        return sum(os.path.getsize(f) for f in self.files if os.path.exists(f))


def load_case(path):
    path = os.path.abspath(path)
    doc = {}
    cj = os.path.join(path, "case.json")
    if os.path.exists(cj):
        with open(cj) as f:
            doc = json.load(f)
        fmt = doc.get("format", SUPPORTED_FORMAT)
        if fmt != SUPPORTED_FORMAT:
            raise ValueError("case %s: unsupported format %r" % (path, fmt))
    run = doc.get("run", {})
    names = run.get("files")
    if names is None:
        names = sorted(n for n in os.listdir(path)
                       if n.endswith(VHDL_EXT + SV_EXT))
        names.sort(key=lambda n: (0 if "pkg" in n or "package" in n else 1, n))
    if not names:
        raise ValueError("case %s has no source files" % path)
    lang = run.get("lang")
    if lang is None:
        lang = "vhdl" if names[0].endswith(VHDL_EXT) else "sv"
    stop = run.get("stop_time_ns")
    return Case(dir=path, name=os.path.basename(path),
                lang=lang, std=str(run.get("std", "2008")),
                top=run.get("top", "tb"),
                files=[os.path.join(path, n) for n in names],
                stop_time=("%dns" % stop) if stop is not None else "10us",
                waves=bool(run.get("observe", {}).get("vcd", True)),
                end_marker=run.get("end_marker", ""), meta=run)
