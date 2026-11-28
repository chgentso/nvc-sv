import copy
import json
import unittest

from hdlfuzz import ir
from hdlfuzz.ir import (TLogic, TBits, TBool, Ref, Const, Index, Slice, Concat,
                        Unop, Binop, Shift, Resize, Conv, Assign, If, Arm, Case,
                        Alt, Port, Signal, CAssign, Comb, FF, Reset, Inst,
                        PortMap, Monitor, Module, Design, FuzzCase, Testbench,
                        TbReset, Input, Observe)

L = TLogic()
U8 = TBits(8)
S8 = TBits(8, signed=True)
V8 = TBits(8, slv=True)


def small_case():
    """A hand-built legal case: child register + parent comb logic."""
    child = Module("m1", [Port("clk", "in", L), Port("rst", "in", L),
                          Port("i0", "in", U8), Port("o0", "out", U8)],
                   [Signal("s0", U8)],
                   [FF("p0", "clk", "rise",
                       Reset("rst", "1", "sync", [Assign(Ref(U8, "s0"), Const(U8, "00000000"))]),
                       [], [Assign(Ref(U8, "s0"),
                                   Binop(U8, "add", Ref(U8, "s0"), Ref(U8, "i0")))]),
                    CAssign("a0", Ref(U8, "o0"), Ref(U8, "s0")),
                    Monitor("mon0", "s0")])
    top = Module("m0", [Port("clk", "in", L), Port("rst", "in", L),
                        Port("i0", "in", V8), Port("o0", "out", L)],
                 [Signal("s0", U8), Signal("s1", U8), Signal("s2", L)],
                 [CAssign("a0", Ref(U8, "s0"), Conv(U8, Ref(V8, "i0"))),
                  Inst("u0", "m1", [PortMap("clk", Ref(L, "clk")),
                                    PortMap("rst", Ref(L, "rst")),
                                    PortMap("i0", Ref(U8, "s0")),
                                    PortMap("o0", Ref(U8, "s1"))]),
                  Comb("p0", None, [],
                       [Assign(Ref(L, "s2"), Const(L, "0")),
                        If([Arm(Binop(TBool(), "lt", Ref(U8, "s1"), Ref(U8, "s0")),
                                [Assign(Ref(L, "s2"), Index(L, Ref(U8, "s1"), 7))])])]),
                  CAssign("a1", Ref(L, "o0"), Ref(L, "s2"))])
    tb = Testbench("clk", 20, 4, TbReset("rst", "1", 1),
                   [Input("i0", "rise", 0, ["00000001", "00000010", "11111111", "00000000"])],
                   Observe())
    return FuzzCase(ir.SCHEMA, 0, "V1", "default", [], Design("m0", [child, top]), tb)


def top_of(case):
    return case.design.modules[-1]


class TestRoundTrip(unittest.TestCase):
    def test_json(self):
        c = small_case()
        j = ir.to_json(c)
        text = json.dumps(j, sort_keys=True)
        c2 = ir.from_json(json.loads(text))
        self.assertEqual(c, c2)
        self.assertEqual(json.dumps(ir.to_json(c2), sort_keys=True), text)

    def test_unknown_kind(self):
        with self.assertRaises(ValueError):
            ir.from_json({"k": "nonsense"})
        with self.assertRaises(ValueError):
            ir.from_json({"k": "logic", "extra": 1})


class TestCheck(unittest.TestCase):
    def test_legal(self):
        self.assertEqual(ir.errors(small_case()), [])

    def bad(self, mutate, fragment):
        c = small_case()
        mutate(c)
        errs = ir.errors(c)
        self.assertTrue(errs, "mutation was accepted")
        self.assertTrue(any(fragment in e for e in errs),
                        "%r not in %r" % (fragment, errs))

    def test_type_mismatch(self):
        def m(c):
            top_of(c).items[0].value = Ref(V8, "i0")
        self.bad(m, "assignment of")

    def test_stored_type_wrong(self):
        def m(c):
            top_of(c).items[3].value.t = U8
        self.bad(m, "stored type")

    def test_arith_on_slv(self):
        def m(c):
            top_of(c).items[0].value = Conv(U8, Binop(V8, "add", Ref(V8, "i0"), Ref(V8, "i0")))
        self.bad(m, "operator add")

    def test_unknown_name(self):
        def m(c):
            top_of(c).items[0].value = Ref(U8, "nope")
        self.bad(m, "unknown name")

    def test_slice_range(self):
        def m(c):
            top_of(c).items[0].value = Resize(U8, Slice(TBits(4), Ref(U8, "s1"), 9, 6))
        self.bad(m, "out of range")

    def test_index_range(self):
        def m(c):
            top_of(c).items[2].body[1].arms[0].body[0].value = Index(L, Ref(U8, "s1"), 8)
        self.bad(m, "out of range")

    def test_const_width(self):
        def m(c):
            c.design.modules[0].items[0].reset.body[0].value = Const(U8, "0101")
        self.bad(m, "bad 8-bit constant")

    def test_two_drivers(self):
        def m(c):
            top_of(c).items.append(CAssign("a9", Ref(U8, "s0"), Ref(U8, "s1")))
        self.bad(m, "several drivers")

    def test_comb_loop(self):
        def m(c):
            top_of(c).items[0] = CAssign("a0", Ref(U8, "s0"), Ref(U8, "s1"))
        self.bad(m, "combinational loop")

    def test_drive_input(self):
        def m(c):
            top_of(c).items.append(CAssign("a9", Ref(V8, "i0"), Const(V8, "00000000")))
        self.bad(m, "not writable")

    def test_output_undriven(self):
        def m(c):
            del top_of(c).items[3]
        self.bad(m, "not driven")

    def test_missing_default(self):
        def m(c):
            del top_of(c).items[2].body[0]
        self.bad(m, "no default assignment")

    def test_sensitivity(self):
        def m(c):
            top_of(c).items[2].sens = ["s1"]
        self.bad(m, "sensitivity list misses")

    def test_comb_reads_own_output(self):
        def m(c):
            p = top_of(c).items[2]
            p.body[1].arms[0].cond = Binop(TBool(), "eq", Ref(L, "s2"), Const(L, "1"))
        self.bad(m, "reads a signal it drives")

    def test_case_coverage(self):
        def m(c):
            p = top_of(c).items[2]
            p.body.append(Case(Ref(L, "clk"), [Alt(["1"], [])], None))
        self.bad(m, "does not cover")

    def test_duplicate_choice(self):
        def m(c):
            p = top_of(c).items[2]
            p.body.append(Case(Ref(L, "clk"), [Alt(["1"], []), Alt(["1"], [])], []))
        self.bad(m, "duplicate choice")

    def test_illegal_ident(self):
        def m(c):
            top_of(c).signals[0].name = "signal"
        self.bad(m, "illegal identifier")

    def test_duplicate_name(self):
        def m(c):
            top_of(c).items[0].label = "s1"
        self.bad(m, "duplicate name")

    def test_inst_port_type(self):
        def m(c):
            top_of(c).items[1].ports[2].actual = Ref(V8, "i0")
        self.bad(m, "actual of")

    def test_inst_unmapped(self):
        def m(c):
            del top_of(c).items[1].ports[2]
        self.bad(m, "not mapped")

    def test_module_order(self):
        def m(c):
            c.design.modules.reverse()
        self.bad(m, "unknown or later module")

    def test_ff_clock_type(self):
        def m(c):
            c.design.modules[0].items[0].clock = "i0"
        self.bad(m, "not a logic name")

    def test_tb_values(self):
        def m(c):
            c.tb.inputs[0].values.pop()
        self.bad(m, "one value per cycle")

    def test_tb_input_undriven(self):
        def m(c):
            c.tb.inputs = []
        self.bad(m, "not driven")

    def test_concat_kind(self):
        def m(c):
            top_of(c).items[0].value = Concat(U8, [Slice(TBits(4, slv=True), Ref(V8, "i0"), 3, 0),
                                                   Slice(TBits(4), Ref(U8, "s1"), 3, 0)])
        self.bad(m, "another kind")

    def test_malformed(self):
        def m(c):
            top_of(c).items[0].value = 42
        self.bad(m, "not an expression")

    def test_shift_and_mul_types(self):
        c = small_case()
        top_of(c).items[0].value = Resize(U8, Binop(TBits(12), "mul", Conv(TBits(4), Slice(TBits(4, slv=True), Ref(V8, "i0"), 3, 0)),
                                                    Shift(U8, "rol", Conv(U8, Ref(V8, "i0")), 3)))
        self.assertEqual(ir.errors(c), [])
        top_of(c).items[0].value.a.t = TBits(16)
        self.assertTrue(ir.errors(c))

    def test_deepcopy_equal(self):
        c = small_case()
        self.assertEqual(copy.deepcopy(c), c)


if __name__ == "__main__":
    unittest.main()
