"""Testbench IR: clock, reset, per-cycle input table with a timing mode per
input, and the observer switches.  The printers turn it into a testbench
(see FORMAT.md for the timing of every event)."""

from .ir import TLogic, TBits, Testbench, TbReset, Input

PERIOD_NS = 20


def gen_tb(rng, design, reset, observe, profile):
    top = next(m for m in design.modules if m.name == design.top)
    cycles = rng.range(20, 80)
    tb_reset = None
    if reset is not None:
        tb_reset = TbReset(reset[0], reset[1], rng.range(1, 3))
    modes = rng.sample(["fall", "rise", "delta"], rng.range(1, 3))
    inputs = []
    for p in top.ports:
        if p.dir != "in" or p.name == "clk" or (reset and p.name == reset[0]):
            continue
        timing = rng.choice(modes)
        delta = rng.range(1, 3) if timing == "delta" else 0
        hold = rng.choice([0, 20, 50, 80])
        vals = []
        for c in range(cycles):
            if vals and rng.chance(hold):
                vals.append(vals[-1])
            else:
                vals.append(_value(rng, p.t))
        inputs.append(Input(p.name, timing, delta, vals))
    return Testbench("clk", PERIOD_NS, cycles, tb_reset, inputs, observe)


def _value(rng, t):
    if isinstance(t, TLogic):
        return rng.choice("01")
    assert isinstance(t, TBits)
    k = rng.below(10)
    if k == 0:
        return "0" * t.w
    if k == 1:
        return "1" * t.w
    return rng.bits(t.w)
