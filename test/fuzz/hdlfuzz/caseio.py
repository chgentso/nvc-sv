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

The generator lane defines the real directory layout (FORMAT.md); the runner
only needs the few facts in Case below.  Everything that depends on the
layout lives in load_case(), so a layout change is a change to this file only.

Layout understood here (the hand-written cases use it, and a generator can
write the same file next to its sources):

    <case dir>/case.json   optional; keys: lang ("vhdl"|"sv"), std ("2008"),
                           top ("tb"), files (relative names, in analysis
                           order), stop_time ("10us"), waves (bool)
    <case dir>/*.vhd|*.vhdl|*.sv|*.v    sources, used when "files" is absent
                           (sorted by name; package-like names first)
"""

import json
import os
from dataclasses import dataclass, field
from typing import List

VHDL_EXT = (".vhd", ".vhdl")
SV_EXT = (".sv", ".v")


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
    meta: dict = field(default_factory=dict)

    def source_size(self):
        return sum(os.path.getsize(f) for f in self.files if os.path.exists(f))


def load_case(path):
    path = os.path.abspath(path)
    cfg = {}
    cj = os.path.join(path, "case.json")
    if os.path.exists(cj):
        with open(cj) as f:
            cfg = json.load(f)
    names = cfg.get("files")
    if names is None:
        names = sorted(n for n in os.listdir(path)
                       if n.endswith(VHDL_EXT + SV_EXT))
        names.sort(key=lambda n: (0 if "pkg" in n or "package" in n else 1, n))
    if not names:
        raise ValueError("case %s has no source files" % path)
    lang = cfg.get("lang")
    if lang is None:
        lang = "vhdl" if names[0].endswith(VHDL_EXT) else "sv"
    return Case(dir=path, name=os.path.basename(path),
                lang=lang, std=str(cfg.get("std", "2008")),
                top=cfg.get("top", "tb"),
                files=[os.path.join(path, n) for n in names],
                stop_time=cfg.get("stop_time", "10us"),
                waves=bool(cfg.get("waves", True)), meta=cfg)
