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


import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hdlfuzz import compare, vcd
from hdlfuzz.run import RunResult

NVC_TEXT = """\
** Note: 4ns+2: d=0A tick=2
    Process :tb:p_mon at tb.vhd:55
** Note: 4ns+2: a=01
** Note: 6ns+1: d=0B tick=1
plain output line
"""

VCD_A = """\
$date today $end
$timescale 1 fs $end
$scope module tb $end
$var wire 1 ! clk $end
$var wire 8 " q [7:0] $end
$upscope $end
$enddefinitions $end
#0
0!
b00000000 "
#5
1!
b1 "
b10 "
#10
0!
"""


def write(d, name, text):
    p = os.path.join(d, name)
    with open(p, "w") as f:
        f.write(text)
    return p


class ReportTests(unittest.TestCase):
    def test_parse(self):
        items = compare.parse_nvc(NVC_TEXT)
        self.assertEqual(len(items), 4)
        self.assertEqual(items[0].delta, 2)
        self.assertIn("Process :tb:p_mon", items[0].msg)
        self.assertEqual(items[3], "plain output line")

    def test_n1_sort(self):
        a = compare.parse_nvc("** Note: 1ns+1: b\n** Note: 1ns+1: a\n"
                              "** Note: 1ns+2: c\n")
        s = compare.n1_sort(a)
        self.assertEqual([r.msg for r in s], ["a", "b", "c"])

    def test_equal_after_n1(self):
        a = RunResult("default", text="** Note: 1ns+1: b\n** Note: 1ns+1: a\n")
        b = RunResult("ref", text="** Note: 1ns+1: a\n** Note: 1ns+1: b\n")
        self.assertIsNone(compare.compare_reports(a, b))
        d = compare.compare_reports(a, b, stable=True)
        self.assertIsNotNone(d)
        self.assertEqual(d.rules, [compare.RULE_PROC, compare.RULE_IEEE])

    def test_delta_shift(self):
        a = RunResult("default", text="** Note: 4ns+1: d=0A tick=1\n")
        b = RunResult("ref", text="** Note: 4ns+3: d=0A tick=1\n")
        d = compare.compare_reports(a, b)
        self.assertEqual(d.kind, "delta-shift")
        self.assertEqual(d.signature(),
                         "l1-report/default-vs-ref/delta-shift/note:d=# tick=#")

    def test_text_change_and_missing(self):
        a = RunResult("x", text="** Note: 4ns+1: d=0A tick=1\n")
        b = RunResult("ref", text="** Note: 4ns+1: d=0A tick=3\n")
        self.assertEqual(compare.compare_reports(a, b).kind, "text-change")
        a = RunResult("x", text="** Note: 4ns+1: p\n")
        b = RunResult("ref", text="** Note: 4ns+1: p\n** Note: 5ns+1: q\n")
        self.assertEqual(compare.compare_reports(a, b).kind, "missing-record")
        self.assertEqual(compare.compare_reports(b, a).kind, "extra-record")

    def test_time_shift(self):
        a = RunResult("x", text="** Note: 5ns+1: p\n")
        b = RunResult("ref", text="** Note: 4ns+1: p\n")
        self.assertEqual(compare.compare_reports(a, b).kind, "time-shift")

    def test_process_line_ignored(self):
        # a fused process is named differently in the trace
        a = RunResult("default", text="** Note: 1ns+1: m\n   Process :tb:dut at "
                      "tb.vhd:14\n")
        b = RunResult("ref", text="** Note: 1ns+1: m\n   Process :tb:dut:_p3 "
                      "at dut.vhd:52\n")
        self.assertIsNone(compare.compare_reports(a, b))
        a = RunResult("default", text="** Note: 1ns+1: m\n   Function F at x:1\n")
        b = RunResult("ref", text="** Note: 1ns+1: m\n   Function G at x:1\n")
        self.assertEqual(compare.compare_reports(a, b).kind, "text-change")

    def test_finish_note_is_not_cross_tool_difference(self):
        n = RunResult("ref", text="** Note: 9ns+0: FINISH called\n")
        g = RunResult("ghdl", text="")
        self.assertIsNone(compare.compare_reports_cross(n, g))

    def test_ghdl_records(self):
        g = ("tb.vhd:55:13:@4ns:(report note): d=0A tick=2\n"
             "tb.vhd:12:5:@50ns:(assertion failure): bad\n"
             "ghdl:error: simulation failed\n")
        r = compare.parse_ghdl(g)
        self.assertEqual(r, [(4 * 10**6, "note", "d=0A tick=2"),
                             (50 * 10**6, "failure", "bad")])

    def test_cross_report(self):
        n = RunResult("ref", text="** Note: 4ns+1: p\n** Note: 4ns+2: q\n"
                      "** Warning: 4ns+2: NUMERIC_STD metavalue detected\n")
        g = RunResult("ghdl", text="a.vhd:1:1:@4ns:(report note): q\n"
                      "a.vhd:1:1:@4ns:(report note): p\n")
        self.assertIsNone(compare.compare_reports_cross(n, g))
        g2 = RunResult("ghdl", text="a.vhd:1:1:@4ns:(report note): p\n")
        d = compare.compare_reports_cross(n, g2)
        self.assertEqual(d.kind, "extra-record")


class VcdTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def test_parse_and_extend(self):
        p = write(self.tmp.name, "a.vcd", VCD_A)
        v = vcd.parse(p)
        self.assertEqual(v.events["tb.q"],
                         [(0, "00000000"), (5, "00000001"), (5, "00000010")])
        self.assertEqual(v.width["tb.q"], 8)
        self.assertEqual(vcd.final_series(v.events["tb.q"]),
                         [(0, "00000000"), (5, "00000010")])

    def test_hash_ignores_date_and_order(self):
        p1 = write(self.tmp.name, "a.vcd", VCD_A)
        t2 = VCD_A.replace("today", "tomorrow").replace(
            '1!\nb1 "\nb10 "', 'b1 "\nb10 "\n1!')
        p2 = write(self.tmp.name, "b.vcd", t2)
        h1, h2 = vcd.hashes(p1), vcd.hashes(p2)
        self.assertEqual(h1[1], h2[1])
        self.assertNotEqual(h1[0], h2[0])

    def test_reduce(self):
        self.assertEqual(vcd.reduce_value("UWhL-1xz"), "xx10x1xz")

    def test_nvc_pair_missing_event(self):
        # the chain bug: the intermediate glitch of q is gone
        p1 = write(self.tmp.name, "a.vcd", VCD_A.replace('b1 "\n', ''))
        p2 = write(self.tmp.name, "b.vcd", VCD_A)
        a = RunResult("default", wave=p1)
        b = RunResult("ref", wave=p2)
        d, strict_eq = compare.compare_vcd_nvc(a, b)
        self.assertFalse(strict_eq)
        self.assertEqual(d.signature(),
                         "l1-vcd/default-vs-ref/missing-event/vec/within-time")

    def test_nvc_pair_value_and_equal(self):
        p2 = write(self.tmp.name, "b.vcd", VCD_A)
        a = RunResult("default", wave=p2)
        self.assertEqual(compare.compare_vcd_nvc(a, a), (None, True))
        p1 = write(self.tmp.name, "a.vcd", VCD_A.replace('b10 "', 'b11 "'))
        d, _ = compare.compare_vcd_nvc(RunResult("x", wave=p1),
                                       RunResult("ref", wave=p2))
        self.assertEqual(d.kind, "value")

    def test_cross_tool(self):
        # other tool: no top scope name, 'U' for x, different id codes,
        # extra signal (array) only on one side, glitch inside a time step
        g = """\
$scope module top $end
$var reg 1 a clk $end
$var reg 8 b q [7:0] $end
$upscope $end
$enddefinitions $end
#0
0a
bUUUUUUUU b
#5
1a
b00000010 b
"""
        n = """\
$scope module tb $end
$var wire 1 ! clk $end
$var wire 8 " q [7:0] $end
$var wire 8 # extra $end
$upscope $end
$enddefinitions $end
#0
0!
bxxxxxxxx "
b00000000 #
#5
1!
b1 "
b10 "
"""
        pg = write(self.tmp.name, "g.vcd", g)
        pn = write(self.tmp.name, "n.vcd", n)
        d, ncommon = compare.compare_vcd_cross(RunResult("ref", wave=pn),
                                               RunResult("ghdl", wave=pg))
        self.assertIsNone(d)
        self.assertEqual(ncommon, 2)
        pg2 = write(self.tmp.name, "g2.vcd", g.replace("00000010", "00000011"))
        d, _ = compare.compare_vcd_cross(RunResult("ref", wave=pn),
                                         RunResult("ghdl", wave=pg2))
        self.assertEqual(d.signature(),
                         "l3-vcd/ref-vs-ghdl/value/vec/later")
        self.assertIn(compare.RULE_9VAL, d.rules)

    def test_cross_tool_early_end(self):
        # fatal at 45: nvc has dumped a change inside the failing time step,
        # the other tool stopped before dumping it
        hdr = """\
$scope module top $end
$var reg 1 a clk $end
$var reg 4 b n [3:0] $end
$upscope $end
$enddefinitions $end
#0
0a
b0000 b
#40
1a
"""
        n = write(self.tmp.name, "n.vcd", hdr + "#45\nb0100 b\n")
        g = write(self.tmp.name, "g.vcd", hdr + "#45\n")
        a = RunResult("ref", wave=n, outcome="failure")
        b = RunResult("ghdl", wave=g, outcome="failure")
        d, _ = compare.compare_vcd_cross(a, b)
        self.assertEqual(d.kind, "extra-event")        # spurious without cut
        d, ncommon = compare.compare_vcd_cross(a, b, cut=True)
        self.assertIsNone(d)
        self.assertEqual(ncommon, 2)
        # a real earlier difference is still seen with the cut
        g2 = write(self.tmp.name, "g2.vcd", hdr.replace("b0000", "b0001") + "#45\n")
        d, _ = compare.compare_vcd_cross(a, RunResult("ghdl", wave=g2),
                                         cut=True)
        self.assertEqual(d.kind, "value")
        self.assertIn(compare.RULE_CUT, d.rules)
        # compare_case switches the cut on for failing runs
        v = compare.compare_case({"ref": a, "ghdl": b})
        self.assertEqual(v.signatures(), [])
        ok = RunResult("ref", wave=n, outcome="ok")
        okg = RunResult("ghdl", wave=g, outcome="ok")
        self.assertEqual(len(compare.compare_case(
            {"ref": ok, "ghdl": okg}).signatures()), 1)


class VerdictTests(unittest.TestCase):
    def test_ieee_warnings_apart(self):
        w = ("** Warning: 5ns+1: NUMERIC_STD.\"<=\": metavalue detected, "
             "returning FALSE\n   Function \"<=\" at x:1\n")
        note = "** Note: 5ns+1: FZ M m0.s1 'U'\n"
        ref = RunResult("ref", text=note)
        isl = RunResult("default", text=w + note)
        # an extra IEEE warning is its own signature and does not hide or
        # shift the comparison of the design's own reports
        self.assertIsNone(compare.compare_reports(isl, ref))
        d = compare.compare_reports(isl, ref, ieee=True)
        self.assertEqual(d.kind, "extra-record")
        v = compare.compare_case({"ref": ref, "default": isl})
        self.assertEqual(v.signatures(), [
            "l1-ieee/default-vs-ref/extra-record/warning:numeric_std.'_': "
            "metavalue detected, returning f"])
        # a real difference in the notes next to it is still a report one
        isl2 = RunResult("default", text=w + "** Note: 5ns+1: FZ M m0.s1 '0'\n")
        v = compare.compare_case({"ref": ref, "default": isl2})
        self.assertEqual(len(v.diffs), 2)
        self.assertEqual(v.diffs[0].what, "report")
        self.assertTrue(compare.is_ieee_warning(compare.parse_nvc(w)[0]))
        self.assertFalse(compare.is_ieee_warning(compare.parse_nvc(
            "** Warning: 1ns+0: user warning\n")[0]))

    def test_report_order_info(self):
        ref = RunResult("ref", text="** Note: 1ns+1: a\n** Note: 1ns+1: b\n")
        same = RunResult("default", text=ref.text)
        swapped = RunResult("nofast",
                            text="** Note: 1ns+1: b\n** Note: 1ns+1: a\n")
        v = compare.compare_case({"ref": ref, "default": same,
                                  "nofast": swapped})
        self.assertEqual(v.signatures(), [])
        self.assertTrue(v.info["default.reports_strict_equal"])
        self.assertFalse(v.info["nofast.reports_strict_equal"])

    def test_signatures_grouped_with_configs(self):
        ref = RunResult("ref", text="** Note: 1ns+1: a\n")
        res = {"ref": ref}
        for c in ("default", "nofast", "spec"):
            res[c] = RunResult(c, text="** Note: 1ns+3: a\n")
        v = compare.compare_case(res)
        self.assertEqual(len(v.signatures()), 3)
        g = v.signatures(group=True)
        self.assertEqual(g, ["l1-report/islands-vs-ref/delta-shift/note:a"])
        self.assertEqual(v.configs()[g[0]], ["default", "nofast", "spec"])
        self.assertEqual(v.primary(True), g[0])

    def test_outcome_and_blame_free(self):
        ok = RunResult("ref", text="")
        bad = RunResult("default", outcome="failure", rc=1, text="")
        v = compare.compare_case({"ref": ok, "default": bad})
        self.assertEqual(v.signatures(),
                         ["l0-outcome/default-vs-ref/failure-ok"])

    def test_crash_not_compared_further(self):
        ok = RunResult("ref")
        cr = RunResult("specA", outcome="crash", stage="run", rc=-11,
                       text="[0x55d1] ../src/rt/island.c:12 rt_island_run\n"
                            "[0x55d2] ../src/rt/model.c:1 run_cycle\n")
        v = compare.compare_case({"ref": ok, "specA": cr})
        self.assertEqual(v.signatures(),
                         ["l0-crash/speca/rt_island_run+run_cycle"])

    def test_ghdl_unavailable_is_note(self):
        g = RunResult("ghdl", outcome="reject", available=False)
        v = compare.compare_case({"ref": RunResult("ref"), "ghdl": g})
        self.assertEqual(v.diffs, [])
        self.assertTrue(v.notes)


if __name__ == "__main__":
    unittest.main()
