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


"""Plumbing tests with a stub nvc and a stub ghdl (no real simulator)."""

import json
import os
import stat
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hdlfuzz import campaign, caseio, compare, run, runcli

STUB_NVC = '''#!{py}
import os, sys
a = sys.argv[1:]
if "--help" in a:
    print("usage: nvc [options]  --report-order --rtl-islands --no-island-fast")
    sys.exit(0)
if "--version" in a:
    print("nvc 0.0 (stub)")
    sys.exit(0)
work = [x for x in a if x.startswith("--work=")][0][7:]
os.makedirs(work, exist_ok=True)
if "-a" in a:
    sys.exit(0)
if "-e" in a:
    open(os.path.join(work, "elab"), "w").write(" ".join(a) + "\\n" +
        ("SPEC" if os.environ.get("NVC_ISLAND_SPEC") else ""))
    sys.exit(0)
if "-r" in a:
    elab = open(os.path.join(work, "elab")).read()
    off = "--rtl-islands=off" in elab
    mode = os.environ.get("STUB_MODE", "")
    if mode == "crash" and "SPEC" in elab:
        sys.stderr.write("** Fatal: internal error\\n*** Caught signal 11\\n"
                         "[0x1] ../src/rt/island.c:1 island_boom\\n"
                         "[0x2] ../src/rt/model.c:2 model_cycle\\n")
        sys.exit(1)
    delta = 3 if off else 1
    if mode != "collapse":
        delta = 3
    print("** Note: 4ns+%d: d=0A tick=3\\n    Process :tb:p at tb.vhd:5" %
          delta, file=sys.stderr)
    w = [x for x in a if x.startswith("--wave=")]
    if w:
        glitch = "" if (mode == "collapse" and not off) else 'b1 "\\n'
        open(w[0][7:], "w").write(
            "$date x $end\\n$scope module tb $end\\n$var wire 8 \\" d [7:0] "
            "$end\\n$upscope $end\\n$enddefinitions $end\\n#0\\nbUUUUUUUU \\"\\n#4\\n"
            + glitch + 'b10 "\\n')
    sys.exit(0)
sys.exit(2)
'''

STUB_GHDL = '''#!{py}
import sys
a = sys.argv[1:]
if "--version" in a:
    print("GHDL stub"); sys.exit(0)
if a[0] in ("-a", "-e"):
    sys.exit(0)
w = [x for x in a if x.startswith("--vcd=")]
print("tb.vhd:5:1:@4ns:(report note): d=0A tick=3")
if w:
    open(w[0][6:], "w").write(
        "$scope module tb $end\\n$var reg 8 ! d [7:0] $end\\n$upscope $end\\n"
        "$enddefinitions $end\\n#0\\nbUUUUUUUU !\\n#4\\nb1 !\\nb10 !\\n")
sys.exit(0)
'''


def mkscript(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text.format(py=sys.executable))
    os.chmod(path, 0o755)


class StubTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        t = self.tmp.name
        mkscript(os.path.join(t, "nvc", "bin", "nvc-sv"), STUB_NVC)
        os.makedirs(os.path.join(t, "nvc", "lib"))
        mkscript(os.path.join(t, "ghdl"), STUB_GHDL)
        cd = os.path.join(t, "cases", "chain")
        os.makedirs(cd)
        with open(os.path.join(cd, "tb.vhd"), "w") as f:
            f.write("-- stub\n")
        self.case = caseio.load_case(cd)
        self.tools = run.Tools(run.Build(os.path.join(t, "nvc")),
                               ghdl=os.path.join(t, "ghdl"),
                               cache_root=os.path.join(t, "cache"), timeout=20)

    def run_case(self, mode, cfgs=run.DEFAULT_CONFIGS):
        os.environ["STUB_MODE"] = mode
        self.addCleanup(os.environ.pop, "STUB_MODE", None)
        return run.run_case(self.tools, self.case, cfgs,
                            os.path.join(self.tmp.name, "w"))

    def test_probe(self):
        caps = run.probe_build(self.tools.build)
        self.assertTrue(caps["report_order"])
        self.assertIn("stub", self.tools.build.version)

    def test_equal(self):
        res = self.run_case("")
        self.assertEqual({r.outcome for r in res.values()}, {"ok"})
        v = compare.compare_case(res)
        self.assertEqual(v.signatures(), [])
        self.assertEqual(v.info["ghdl.common_names"], 1)

    def test_collapse_difference(self):
        res = self.run_case("collapse")
        v = compare.compare_case(res)
        sigs = v.signatures()
        self.assertIn("l1-report/default-vs-ref/delta-shift/note:d=# tick=#",
                      sigs)
        self.assertIn("l1-vcd/default-vs-ref/missing-event/vec/within-time",
                      sigs)
        self.assertEqual(v.blame, "no-witness")
        self.assertFalse(any(s.startswith("l1-report/ref") for s in sigs))

    def test_crash_only_in_spec(self):
        res = self.run_case("crash")
        self.assertEqual(res["ref"].outcome, "ok")
        self.assertEqual(res["spec"].outcome, "crash")
        self.assertEqual(res["specA"].outcome, "crash")
        v = compare.compare_case(res)
        self.assertIn("l0-crash/spec/island_boom+model_cycle", v.signatures())

    def test_no_ghdl(self):
        self.tools.ghdl = None
        res = self.run_case("", ("ref", "default", "ghdl"))
        self.assertFalse(res["ghdl"].available)
        self.assertEqual(compare.compare_case(res).diffs, [])

    def test_timeout_kills_group(self):
        t0 = time.time()
        rc, text, to, _ = run.call(["sh", "-c", "sleep 30 & sleep 30"],
                                   self.tmp.name, dict(os.environ), 1)
        self.assertTrue(to)
        self.assertEqual(rc, -9)
        self.assertLess(time.time() - t0, 10)

    def test_private_jit_cache(self):
        env = run.worker_env(self.tools)
        self.assertTrue(env["NVC_JIT_CACHE_DIR"].startswith(
            self.tools.cache_root))
        self.assertTrue(os.path.isdir(env["NVC_JIT_CACHE_DIR"]))

    def test_run_one_and_buckets(self):
        os.environ["STUB_MODE"] = "collapse"
        self.addCleanup(os.environ.pop, "STUB_MODE", None)
        out = os.path.join(self.tmp.name, "out")
        os.makedirs(out)
        rec = campaign.run_one(self.tools, self.case, run.DEFAULT_CONFIGS,
                               out, os.path.join(out, "w0"), "V1", 7,
                               stats=False)
        self.assertTrue(rec["reproduced"])
        self.assertTrue(rec["primary"].startswith("l1-report/"))
        campaign.append_line(os.path.join(out, "cases.jsonl"), rec)
        b = campaign.write_buckets(out)
        self.assertEqual(b[rec["primary"]]["count"], 1)
        fails = os.listdir(os.path.join(out, "fail"))
        self.assertEqual(len(fails), 1)
        kept = os.path.join(out, "fail", fails[0], "chain")
        self.assertTrue(os.path.exists(os.path.join(kept, "verdict.json")))
        self.assertTrue(os.path.exists(os.path.join(kept, "tb.vhd")))
        self.assertFalse(os.path.exists(os.path.join(out, "w0", "case-chain")))

    def test_passing_case_leaves_nothing(self):
        out = os.path.join(self.tmp.name, "out")
        os.makedirs(out)
        rec = campaign.run_one(self.tools, self.case, run.DEFAULT_CONFIGS,
                               out, os.path.join(out, "w0"), stats=False)
        self.assertEqual(rec["signatures"], [])
        self.assertFalse(os.path.exists(os.path.join(out, "fail")))


class CampaignGuardTests(unittest.TestCase):
    def test_refuses_big_campaign(self):
        os.environ.pop("HDLFUZZ_ALLOW_CAMPAIGN", None)
        with self.assertRaises(SystemExit) as cm:
            runcli.main(["campaign", "--build", "/nonexistent", "--out", "/x",
                         "--seeds", "1..21", "--dry-run"])
        self.assertIn("owner", str(cm.exception))

    def test_dry_run_commands(self):
        os.environ["HDLFUZZ_SLOT_SH"] = "slot.sh"
        self.addCleanup(os.environ.pop, "HDLFUZZ_SLOT_SH", None)
        cmds = campaign.worker_commands(lambda i, n: ["--shard", "%d/%d" %
                                                      (i, n)])
        self.assertEqual(len(cmds), 2)
        self.assertEqual(cmds[0][0][:2], ["slot.sh", "3G"])
        self.assertIn("hdlfuzz.runcli", cmds[1][0])
        self.assertEqual(cmds[1][0][-1], "1/2")

    def test_default_scope_cap(self):
        os.environ.pop("HDLFUZZ_SLOT_SH", None)
        c = campaign.slot_prefix()
        self.assertIn("MemoryMax=3G", c)
        self.assertIn("MemorySwapMax=0", c)

    def test_stop_file(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(campaign.stop_reason(d))
            open(os.path.join(d, "STOP"), "w").close()
            self.assertEqual(campaign.stop_reason(d), "STOP file")


class CaseIoTests(unittest.TestCase):
    def test_infer(self):
        with tempfile.TemporaryDirectory() as d:
            for n in ("b.vhd", "a_pkg.vhd"):
                open(os.path.join(d, n), "w").close()
            c = caseio.load_case(d)
            self.assertEqual([os.path.basename(f) for f in c.files],
                             ["a_pkg.vhd", "b.vhd"])
            self.assertEqual((c.lang, c.std, c.top), ("vhdl", "2008", "tb"))

    def test_format1(self):
        with tempfile.TemporaryDirectory() as d:
            for n in ("dut.vhd", "tb.vhd"):
                open(os.path.join(d, n), "w").close()
            doc = {"format": 1, "ir": {"seed": 3},
                   "run": {"files": ["dut.vhd", "tb.vhd"], "top": "fz_tb",
                           "stop_time_ns": 440, "end_marker": "FZ END",
                           "observe": {"vcd": False}, "lang": "vhdl",
                           "std": "2008"}}
            with open(os.path.join(d, "case.json"), "w") as f:
                json.dump(doc, f)
            c = caseio.load_case(d)
            self.assertEqual((c.top, c.stop_time, c.waves, c.end_marker),
                             ("fz_tb", "440ns", False, "FZ END"))
            self.assertEqual([os.path.basename(f) for f in c.files],
                             ["dut.vhd", "tb.vhd"])
            doc["format"] = 2
            with open(os.path.join(d, "case.json"), "w") as f:
                json.dump(doc, f)
            with self.assertRaises(ValueError):
                caseio.load_case(d)

    def test_hand_written_cases(self):
        root = os.path.join(os.path.dirname(__file__), "..", "cases")
        for n in sorted(os.listdir(root)):
            c = caseio.load_case(os.path.join(root, n))
            self.assertEqual(c.top, "fz_tb")
            self.assertTrue(all(os.path.exists(f) for f in c.files))


class EndMarkerTests(unittest.TestCase):
    def test_incomplete(self):
        c = caseio.Case("d", "n", end_marker="FZ END")
        self.assertEqual(run.finished(c, "ok", "** Note: 1ns+0: FZ S 0\n"),
                         "incomplete")
        self.assertEqual(run.finished(c, "ok", "** Note: 1ns+0: FZ END\n"),
                         "ok")
        self.assertEqual(run.finished(c, "crash", ""), "crash")
        self.assertEqual(run.finished(caseio.Case("d", "n"), "ok", ""), "ok")


if __name__ == "__main__":
    unittest.main()
