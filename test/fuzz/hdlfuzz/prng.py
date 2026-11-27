"""Deterministic PRNG for hdlfuzz.

splitmix64 with integer-only helpers, so the stream does not depend on the
Python version (the `random` module gives no such promise).  A case is
identified by (campaign seed, case number); `case_seed` mixes the two.
"""

MASK64 = (1 << 64) - 1
GOLDEN = 0x9E3779B97F4A7C15


def mix64(z):
    """splitmix64 finaliser: a bijection on 64-bit integers."""
    z &= MASK64
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return z ^ (z >> 31)


def case_seed(campaign, case):
    """Seed of case number `case` in campaign `campaign`."""
    return mix64(mix64(campaign & MASK64) ^ ((case * GOLDEN) & MASK64))


class Rng:
    """splitmix64 generator with choice/weight helpers."""

    def __init__(self, seed):
        self.state = seed & MASK64

    def next64(self):
        self.state = (self.state + GOLDEN) & MASK64
        return mix64(self.state)

    def fork(self, tag):
        """Independent sub-stream named by a small integer or string.

        Used so that adding a decision in one part of the generator does not
        shift the stream of another part.
        """
        if isinstance(tag, str):
            h = 0
            for ch in tag.encode("utf-8"):
                h = mix64(h ^ ch)
            tag = h
        return Rng(mix64(self.next64() ^ (tag & MASK64)))

    def below(self, n):
        """Uniform integer in [0, n), unbiased (rejection sampling)."""
        if n <= 0:
            raise ValueError("below() needs n > 0")
        limit = ((1 << 64) // n) * n
        while True:
            x = self.next64()
            if x < limit:
                return x % n

    def range(self, lo, hi):
        """Uniform integer in [lo, hi] (inclusive)."""
        if hi < lo:
            raise ValueError("range() needs lo <= hi")
        return lo + self.below(hi - lo + 1)

    def chance(self, percent):
        """True with probability percent/100 (integer percent)."""
        return self.below(100) < percent

    def choice(self, seq):
        if not seq:
            raise ValueError("choice() from an empty sequence")
        return seq[self.below(len(seq))]

    def weighted(self, pairs):
        """Pick from [(item, weight), ...] with integer weights >= 0."""
        total = sum(w for _, w in pairs)
        if total <= 0:
            raise ValueError("weighted() needs a positive total weight")
        x = self.below(total)
        for item, w in pairs:
            if x < w:
                return item
            x -= w
        raise AssertionError("unreachable")

    def shuffle(self, lst):
        """Fisher-Yates shuffle in place; returns the list."""
        for i in range(len(lst) - 1, 0, -1):
            j = self.below(i + 1)
            lst[i], lst[j] = lst[j], lst[i]
        return lst

    def sample(self, seq, k):
        """k distinct elements of seq, in a random order."""
        lst = list(seq)
        self.shuffle(lst)
        return lst[:k]

    def bits(self, w, alphabet="01"):
        """String of w characters from alphabet (MSB first by convention)."""
        return "".join(alphabet[self.below(len(alphabet))] for _ in range(w))
