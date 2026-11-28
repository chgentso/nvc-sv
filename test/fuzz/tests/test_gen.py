import json
import os
import tempfile
import unittest

from hdlfuzz import casefile, gen, ir


class TestGen(unittest.TestCase):
    def test_deterministic_bytes(self):
        for seed in (0, 1, 12345, 2 ** 63 + 7):
            a = casefile.render(gen.generate(seed))
            b = casefile.render(gen.generate(seed))
            self.assertEqual(a, b)

    def test_seeds_differ(self):
        self.assertNotEqual(casefile.render(gen.generate(1))["dut.vhd"],
                            casefile.render(gen.generate(2))["dut.vhd"])

    def test_legal_and_round_trip(self):
        for seed in range(60):
            c = gen.generate(seed)
            self.assertEqual(ir.errors(c), [], "seed %d" % seed)
            files = casefile.render(c)
            c2 = ir.from_json(json.loads(files["case.json"])["ir"])
            self.assertEqual(c, c2)
            self.assertEqual(casefile.render(c2), files)

    def test_profiles(self):
        for p in sorted(gen.PROFILES):
            for seed in range(5):
                c = gen.generate(seed, profile=p)
                self.assertEqual(c.profile, p)
                self.assertEqual(ir.errors(c), [], "%s seed %d" % (p, seed))

    def test_write_and_load(self):
        c = gen.generate(3)
        with tempfile.TemporaryDirectory() as d:
            files = casefile.write_case(c, d)
            self.assertEqual(sorted(os.listdir(d)), sorted(files))
            self.assertEqual(casefile.load_case(d), c)
            with open(os.path.join(d, "case.json")) as f:
                run = json.load(f)["run"]
            self.assertEqual(run["top"], "fz_tb")
            self.assertEqual(run["files"], ["dut.vhd", "tb.vhd"])

    def test_write_refuses_illegal(self):
        c = gen.generate(4)
        c.tb.inputs = []
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ir.IRError):
                casefile.write_case(c, d)

    def test_unknown_tier(self):
        with self.assertRaises(ValueError):
            gen.generate(0, tier="S1")


if __name__ == "__main__":
    unittest.main()
