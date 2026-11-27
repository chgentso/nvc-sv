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
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hdlfuzz import signatures as sg
from hdlfuzz.run import RunResult, classify

CRASH = """\
** Fatal: internal error
*** Caught signal 11 (SIGSEGV) [address=0x10, ip=0x7f12]

[0x7f1234] ../src/util.c:1234 show_stacktrace
[0x7f1235] (/lib/x86_64-linux-gnu/libc.so.6+0x3c050) __restore_rt
[0x55d0c1] ../src/rt/island.c:812 rt_island_eval_comb
[0x55d0c2] ../src/rt/model.c:3301 model_cycle
[0x55d0c3] ../src/rt/model.c:3400 model_run
"""

ASAN = """\
==1==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020
    #0 0x55d in memcpy (/x/nvc+0x1)
    #1 0x55e in island_copy_wires /src/rt/island.c:99
    #2 0x55f in model_cycle /src/rt/model.c:3301
"""


class MaskTests(unittest.TestCase):
    def test_mask(self):
        self.assertEqual(sg.mask("d=0A tick=12 at tb.vhd:55"),
                         "d=# tick=# at <file>")
        self.assertEqual(sg.mask("signal 'foo' not found"),
                         "signal '_' not found")
        self.assertEqual(sg.mask("x" * 100, 10), "x" * 10)

    def test_mask_report(self):
        # one bug, many values and signal names: one shape
        for a, b in (("FZ M m0.s7 xxxx0000uuuu", "FZ M m12.s3 0101"),
                     ("FZ M m1.s9 'U'", "FZ M m4.s2 xx"),
                     ("FZ S 12 o0=0000 o1=xx", "FZ S 7 o0=1u1u o1='U'")):
            self.assertEqual(sg.mask_report(a), sg.mask_report(b))
        self.assertEqual(sg.mask_report("FZ M m0.s7 xxxx"), "FZ M #")
        self.assertEqual(sg.mask_report("FZ S 12 o0=0000 o1=xx"),
                         "FZ S # o0=# o1=#")
        # words stay
        self.assertEqual(sg.mask_report("signal not found"),
                         "signal not found")

    def test_crash_frames(self):
        self.assertEqual(sg.crash_frames(CRASH),
                         ["rt_island_eval_comb", "model_cycle"])
        self.assertEqual(sg.crash_frames(ASAN),
                         ["memcpy", "island_copy_wires"])

    def test_crash_shape_fallback(self):
        t = "nvc: ../src/x.c:10: Assertion 'a == 3' failed."
        self.assertEqual(sg.crash_shape(t), "assert:a == #")
        self.assertEqual(sg.crash_shape("nothing"), "unknown")


class ClassifyTests(unittest.TestCase):
    def test_classes(self):
        self.assertEqual(classify(0, "", False), "ok")
        self.assertEqual(classify(1, "** Failure: x", False), "failure")
        self.assertEqual(classify(-11, "", False), "crash")
        self.assertEqual(classify(1, CRASH, False), "crash")
        self.assertEqual(classify(0, "", True), "timeout")
        self.assertEqual(classify(1, "** Fatal: sorry, not supported", False),
                         "unsupported")
        self.assertEqual(classify(1, "limit of 1000 delta cycles reached",
                                  False), "deltalimit")


class SingleTests(unittest.TestCase):
    def test_reject(self):
        r = RunResult("default", outcome="reject", stage="analyse", rc=1,
                      text="** Error: tb.vhd:12: no visible declaration "
                           "for 'foo7'\n")
        d = sg.single_difference(r)
        self.assertEqual(d.signature(),
                         "l0-reject/default/no visible declaration for '_'")

    def test_timeout_and_ok(self):
        self.assertEqual(sg.single_difference(
            RunResult("spec", outcome="timeout")).signature(),
            "l0-timeout/spec")
        self.assertIsNone(sg.single_difference(RunResult("ref")))
        self.assertIsNone(sg.single_difference(
            RunResult("ref", outcome="failure")))

    def test_hint(self):
        d = sg.Difference("L1", "vcd", "default", "ref", "extra-event",
                          "vec/within-time", hint="comb-driven")
        self.assertEqual(d.signature(),
                         "l1-vcd/default-vs-ref/extra-event/vec/within-time/"
                         "comb-driven")

    def test_group_island_configs(self):
        a = sg.Difference("L1", "report", "spec", "ref", "extra-record", "x")
        b = sg.Difference("L1", "report", "nofast", "ref", "extra-record", "x")
        self.assertNotEqual(a.signature(), b.signature())
        self.assertEqual(a.signature(True), b.signature(True))
        self.assertEqual(a.signature(True), "l1-report/islands-vs-ref/"
                         "extra-record/x")
        # reference and other tools keep their name
        c = sg.Difference("L3", "vcd", "ref", "ghdl", "value", "vec/later")
        self.assertEqual(c.signature(True), c.signature())
        d = sg.Difference("L0", "crash", "ghdl", shape="f")
        self.assertEqual(d.signature(True), "l0-crash/ghdl/f")
        e = sg.Difference("L0", "crash", "specA", shape="f")
        self.assertEqual(e.signature(True), "l0-crash/islands/f")


if __name__ == "__main__":
    unittest.main()
