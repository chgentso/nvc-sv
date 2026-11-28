"""Case directory format (see ../FORMAT.md): case.json + dut.vhd + tb.vhd."""

import json
import os

from . import ir, emit_vhdl

FORMAT = 1
CASE_JSON = "case.json"
DUT_VHD = "dut.vhd"
TB_VHD = "tb.vhd"


def run_info(case):
    """The "run" block of case.json: everything a runner needs."""
    tb = case.tb
    top = next(m for m in case.design.modules if m.name == case.design.top)
    monitors = []
    for m in case.design.modules:
        for it in ir.walk_items(m.items):
            if isinstance(it, ir.Monitor):
                monitors.append("%s.%s" % (m.name, it.signal))
    return {
        "lang": "vhdl",
        "std": "2008",
        "files": [DUT_VHD, TB_VHD],
        "top": emit_vhdl.TB_NAME,
        "dut_entity": top.name,
        "dut_instance": "dut",
        "period_ns": tb.period_ns,
        "cycles": tb.cycles,
        "end_time_ns": tb.cycles * tb.period_ns + tb.period_ns // 4,
        "stop_time_ns": (tb.cycles + 2) * tb.period_ns,
        "end_marker": "FZ END",
        "outputs": [p.name for p in top.ports if p.dir == "out"],
        "monitors": monitors,
        "observe": ir.to_json(tb.observe),
    }


def dumps(obj):
    return json.dumps(obj, indent=1, sort_keys=True) + "\n"


def case_json(case):
    return dumps({"format": FORMAT, "run": run_info(case), "ir": ir.to_json(case)})


def render(case):
    """{file name: text} of a case directory (deterministic bytes)."""
    return {
        CASE_JSON: case_json(case),
        DUT_VHD: emit_vhdl.design(case.design),
        TB_VHD: emit_vhdl.testbench(case),
    }


def write_case(case, outdir):
    """Check case and write its directory; returns the file map."""
    ir.check(case)
    files = render(case)
    os.makedirs(outdir, exist_ok=True)
    for name, text in files.items():
        with open(os.path.join(outdir, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    return files


def load_case(path):
    """FuzzCase from a case directory or a case.json path."""
    if os.path.isdir(path):
        path = os.path.join(path, CASE_JSON)
    with open(path, encoding="utf-8") as f:
        obj = json.load(f)
    if obj.get("format") != FORMAT:
        raise ValueError("%s: case format %r, expected %d" % (path, obj.get("format"), FORMAT))
    return ir.from_json(obj["ir"])
