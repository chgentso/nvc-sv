import unittest

from hdlfuzz.prng import Rng, case_seed, mix64


class TestPrng(unittest.TestCase):
    def test_known_values(self):
        # splitmix64 reference stream for seed 0
        r = Rng(0)
        self.assertEqual(r.next64(), 0xE220A8397B1DCDAF)
        self.assertEqual(r.next64(), 0x6E789E6AA1B965F4)
        self.assertEqual(r.next64(), 0x06C45D188009454F)

    def test_deterministic(self):
        a, b = Rng(1234), Rng(1234)
        self.assertEqual([a.below(1000) for _ in range(50)],
                         [b.below(1000) for _ in range(50)])

    def test_ranges(self):
        r = Rng(7)
        for _ in range(2000):
            x = r.range(-3, 5)
            self.assertTrue(-3 <= x <= 5)
            self.assertTrue(0 <= r.below(7) < 7)
        self.assertEqual({r.range(0, 3) for _ in range(500)}, {0, 1, 2, 3})
        with self.assertRaises(ValueError):
            r.below(0)
        with self.assertRaises(ValueError):
            r.range(2, 1)

    def test_helpers(self):
        r = Rng(99)
        self.assertEqual(r.weighted([("a", 0), ("b", 5)]), "b")
        lst = list(range(20))
        self.assertEqual(sorted(r.shuffle(list(lst))), lst)
        s = r.sample(lst, 5)
        self.assertEqual(len(set(s)), 5)
        self.assertEqual(len(r.bits(130)), 130)
        self.assertTrue(set(r.bits(64)) <= {"0", "1"})
        self.assertIn(r.choice("xyz"), "xyz")

    def test_fork_and_case_seed(self):
        self.assertNotEqual(Rng(5).fork("a").next64(), Rng(5).fork("b").next64())
        self.assertEqual(Rng(5).fork("a").next64(), Rng(5).fork("a").next64())
        seeds = {case_seed(1, k) for k in range(1000)}
        self.assertEqual(len(seeds), 1000)
        self.assertNotEqual(case_seed(1, 0), case_seed(2, 0))
        self.assertEqual(mix64(0), 0)


if __name__ == "__main__":
    unittest.main()
