"""Seed + tier + profile -> IR, legal by construction (tier V1).

V1: VHDL-2008 synchronous RTL.  Concurrent assignments (simple,
conditional, selected), combinational processes (default assignments
first, if/case), FF processes on one clock (rising or falling edge, sync
reset or none), std_logic, unsigned/signed/std_logic_vector of widths
1-130, numeric_std operators and conversions, slices, concatenation and
hierarchy through ports.

No combinational loops: every combinational driver reads only names that
existed before it (inputs, registers, earlier combinational signals);
registers may read anything.
"""

from . import ir
from .ir import (TLogic, TBits, TBool, Ref, Const, Index, Slice, Concat, Unop,
                 Binop, Shift, Resize, Conv, Assign, If, Arm, Case, Alt,
                 Port, Signal, CAssign, CCond, CArm, CSel, CAlt, Comb, FF,
                 Reset, Inst, PortMap, Monitor, Module, Design, FuzzCase)
from .prng import Rng
from . import tb as tbgen

GEN_VERSION = 1

PROFILES = {
    # n_in, n_out, n_reg, n_comb are inclusive ranges
    "default": dict(n_in=(1, 4), n_out=(1, 3), n_reg=(2, 6), n_comb=(3, 8),
                    depth=3, wide=10, children=(0, 2), hier_depth=2,
                    proc=35, reset=70, init=50),
    "comb": dict(n_in=(2, 4), n_out=(1, 3), n_reg=(1, 3), n_comb=(6, 14),
                 depth=4, wide=8, children=(0, 1), hier_depth=1,
                 proc=40, reset=70, init=50),
    "ff": dict(n_in=(1, 3), n_out=(1, 3), n_reg=(5, 12), n_comb=(1, 4),
               depth=3, wide=8, children=(0, 1), hier_depth=1,
               proc=30, reset=60, init=50),
    "wide": dict(n_in=(1, 3), n_out=(1, 3), n_reg=(2, 5), n_comb=(2, 6),
                 depth=3, wide=60, children=(0, 1), hier_depth=1,
                 proc=35, reset=70, init=50),
    "hier": dict(n_in=(1, 3), n_out=(1, 2), n_reg=(1, 4), n_comb=(1, 4),
                 depth=2, wide=8, children=(1, 3), hier_depth=3,
                 proc=35, reset=70, init=50),
    "uheavy": dict(n_in=(1, 3), n_out=(1, 3), n_reg=(2, 6), n_comb=(2, 6),
                   depth=3, wide=10, children=(0, 1), hier_depth=1,
                   proc=35, reset=0, init=20),
    "tiny": dict(n_in=(1, 2), n_out=(1, 1), n_reg=(1, 2), n_comb=(1, 2),
                 depth=2, wide=5, children=(0, 0), hier_depth=0,
                 proc=40, reset=60, init=50),
}

PROFILE_WEIGHTS = [("default", 40), ("comb", 12), ("ff", 12), ("wide", 10),
                   ("hier", 10), ("uheavy", 8), ("tiny", 8)]

MAX_W = 130


class _Mod:
    """Per-module naming and scope state."""

    def __init__(self, name):
        self.name = name
        self.m = Module(name, [], [], [])
        self.counters = {}
        self.avail = []         # [(name, type)] readable by comb drivers
        self.regs = []          # register signals (driven by FF processes)
        self.combs = []         # comb-driven internal signals

    def fresh(self, prefix):
        k = self.counters.get(prefix, 0)
        self.counters[prefix] = k + 1
        return "%s%d" % (prefix, k)


class Generator:
    def __init__(self, seed, tier="V1", profile=None):
        if tier != "V1":
            raise ValueError("tier %s is not implemented (C1 has V1 only)" % tier)
        self.seed = seed
        self.tier = tier
        self.rng = Rng(seed)
        top = self.rng.fork("profile")
        self.profile = profile or top.weighted(PROFILE_WEIGHTS)
        if self.profile not in PROFILES:
            raise ValueError("unknown profile %r" % self.profile)
        self.p = PROFILES[self.profile]
        self.modules = []
        self.n_mod = 0
        sw = self.rng.fork("swarm")
        # swarm switches: each case enables a random subset of features
        self.sw = {
            "ccond": sw.chance(60), "csel": sw.chance(50),
            "case": sw.chance(60), "mul": sw.chance(50),
            "concat": sw.chance(75), "slice": sw.chance(75),
            "conv": sw.chance(75), "shift": sw.chance(60),
            "reduce": sw.chance(50), "fall_edge": sw.chance(15),
            "explicit_sens": sw.chance(30), "partial_target": sw.chance(40),
            "signed": sw.chance(80), "slv": sw.chance(70),
        }
        self.reset = None
        if sw.chance(self.p["reset"]):
            self.reset = ("rst", sw.choice(["1", "1", "0"]))
        obs = self.rng.fork("observe")
        self.observe = ir.Observe(vcd=obs.chance(85), monitors=obs.chance(85),
                                  sampler=True, final=True)

    # --------------------------------------------------------- types
    def width(self, rng):
        if rng.chance(self.p["wide"]):
            return rng.range(65, MAX_W)
        return self._w(rng)

    def _w(self, rng):
        lo, hi = rng.weighted([((1, 8), 45), ((9, 32), 35), ((33, 64), 20)])
        return rng.range(lo, hi)

    def rtype(self, rng=None, allow_logic=True):
        rng = rng or self.rng
        if allow_logic and rng.chance(18):
            return TLogic()
        w = self.width(rng)
        kinds = [("u", 50)]
        if self.sw["signed"]:
            kinds.append(("s", 30))
        if self.sw["slv"]:
            kinds.append(("v", 25))
        k = rng.weighted(kinds)
        return TBits(w, k == "s", k == "v")

    def const_value(self, t):
        rng = self.rng
        if isinstance(t, TLogic):
            return rng.choice("01")
        if isinstance(t, TBool):
            return rng.choice(["true", "false"])
        if rng.chance(15):
            return rng.choice("01") * t.w
        return rng.bits(t.w)

    def const(self, t):
        return Const(t, self.const_value(t))

    # ---------------------------------------------------- expressions
    def ref(self, name, t):
        return Ref(t, name)

    def leaf(self, t, avail):
        rng = self.rng
        opts = []
        same = [n for n, ty in avail if ty == t]
        if same:
            opts.append(("ref", 60))
        if isinstance(t, TBits):
            wider = [(n, ty) for n, ty in avail if isinstance(ty, TBits)
                     and ty.signed == t.signed and ty.slv == t.slv and ty.w > t.w]
            if wider and self.sw["slice"]:
                opts.append(("slice", 25))
            convs = [(n, ty) for n, ty in avail if isinstance(ty, TBits)
                     and ty.w == t.w and ty != t]
            if convs and self.sw["conv"]:
                opts.append(("conv", 12))
            if not t.slv:
                rs = [(n, ty) for n, ty in avail if isinstance(ty, TBits)
                      and not ty.slv and ty.signed == t.signed and ty.w != t.w]
                if rs:
                    opts.append(("resize", 18))
            if t.slv:
                ns = [(n, ty) for n, ty in avail if isinstance(ty, TBits)
                      and not ty.slv and ty.w != t.w]
                if ns and self.sw["conv"]:
                    opts.append(("conv_resize", 10))
        if isinstance(t, TLogic):
            bits = [(n, ty) for n, ty in avail if isinstance(ty, TBits)]
            if bits:
                opts.append(("index", 35))
        opts.append(("const", 8 if opts else 1))
        k = rng.weighted(opts)
        if k == "ref":
            return Ref(t, rng.choice(same))
        if k == "slice":
            n, ty = rng.choice(wider)
            lo = rng.range(0, ty.w - t.w)
            return Slice(t, Ref(ty, n), lo + t.w - 1, lo)
        if k == "conv":
            n, ty = rng.choice(convs)
            return Conv(t, Ref(ty, n))
        if k == "resize":
            n, ty = rng.choice(rs)
            return Resize(t, Ref(ty, n))
        if k == "conv_resize":
            n, ty = rng.choice(ns)
            mid = TBits(t.w, ty.signed, False)
            return Conv(t, Resize(mid, Ref(ty, n)))
        if k == "index":
            n, ty = rng.choice(bits)
            return Index(t, Ref(ty, n), rng.range(0, ty.w - 1))
        return self.const(t)

    def pick_type(self, avail, pred=None):
        """A type, preferably one some available name has."""
        tys = [ty for _, ty in avail if pred is None or pred(ty)]
        if tys and self.rng.chance(80):
            return self.rng.choice(tys)
        for _ in range(20):
            t = self.rtype()
            if pred is None or pred(t):
                return t
        return TBits(self._w(self.rng))

    def expr(self, t, avail, depth):
        rng = self.rng
        if isinstance(t, TBool):
            # boolean leaves would be constants: always build a comparison
            depth = max(depth, 1)
        elif depth <= 0 or rng.chance(30):
            return self.leaf(t, avail)
        d = depth - 1
        if isinstance(t, TBool):
            opts = [("cmp", 55), ("logic", 25)]
            if depth > 1:
                opts += [("bop", 15), ("bnot", 5)]
            k = rng.weighted(opts)
            if k == "cmp":
                ct = self.pick_type(avail, lambda ty: isinstance(ty, TBits))
                ops = ["eq", "ne"]
                if not ct.slv:
                    ops += ["lt", "le", "gt", "ge"]
                return Binop(TBool(), rng.choice(ops), self.expr(ct, avail, d),
                             self.expr(ct, avail, d))
            if k == "logic":
                return Binop(TBool(), rng.choice(["eq", "ne"]),
                             self.expr(TLogic(), avail, d), self.const(TLogic()))
            if k == "bop":
                return Binop(TBool(), rng.choice(["and", "or", "xor"]),
                             self.expr(t, avail, d), self.expr(t, avail, d))
            return Unop(TBool(), "not", self.expr(t, avail, d))
        if isinstance(t, TLogic):
            opts = [("bit", 30), ("not", 8), ("leaf", 30)]
            if self.sw["reduce"]:
                opts.append(("reduce", 15))
            k = rng.weighted(opts)
            if k == "bit":
                return Binop(t, rng.choice(["and", "or", "xor", "nand", "nor", "xnor"]),
                             self.expr(t, avail, d), self.expr(t, avail, d))
            if k == "not":
                return Unop(t, "not", self.expr(t, avail, d))
            if k == "reduce":
                bt = self.pick_type(avail, lambda ty: isinstance(ty, TBits))
                return Unop(t, rng.choice(["red_and", "red_or", "red_xor", "red_nand",
                                           "red_nor", "red_xnor"]),
                            self.expr(bt, avail, d))
            return self.leaf(t, avail)
        assert isinstance(t, TBits)
        num = not t.slv
        opts = [("bit", 20), ("not", 4), ("leaf", 15)]
        if num:
            opts += [("add", 25), ("resize", 8)]
            if self.sw["shift"]:
                opts.append(("shift", 8))
            if self.sw["mul"] and 2 <= t.w <= 64:
                opts.append(("mul", 8))
            if t.signed:
                opts.append(("neg", 4))
        if self.sw["concat"] and t.w >= 2:
            opts.append(("concat", 10))
        if self.sw["conv"]:
            opts.append(("conv", 8))
        k = rng.weighted(opts)
        if k == "bit":
            return Binop(t, rng.choice(["and", "or", "xor", "nand", "nor", "xnor"]),
                         self.expr(t, avail, d), self.expr(t, avail, d))
        if k == "not":
            return Unop(t, "not", self.expr(t, avail, d))
        if k == "add":
            return Binop(t, rng.choice(["add", "add", "sub"]),
                         self.expr(t, avail, d), self.expr(t, avail, d))
        if k == "resize":
            ot = self.pick_type(avail, lambda ty: isinstance(ty, TBits) and not ty.slv
                                and ty.signed == t.signed and ty.w != t.w)
            if not (isinstance(ot, TBits) and not ot.slv and ot.signed == t.signed
                    and ot.w != t.w):
                ot = TBits(t.w + rng.range(1, 8) if t.w < MAX_W - 8 else t.w - 1,
                           t.signed)
            return Resize(t, self.expr(ot, avail, d))
        if k == "shift":
            return Shift(t, rng.choice(["sll", "srl", "rol", "ror"]),
                         self.expr(t, avail, d), rng.range(0, t.w))
        if k == "mul":
            wa = rng.range(1, t.w - 1)
            return Binop(t, "mul", self.expr(TBits(wa, t.signed), avail, d),
                         self.expr(TBits(t.w - wa, t.signed), avail, d))
        if k == "neg":
            return Unop(t, "neg", self.expr(t, avail, d))
        if k == "concat":
            n = 2 if t.w < 3 else rng.range(2, 3)
            cuts = sorted(rng.sample(range(1, t.w), n - 1))
            sizes = [b - a for a, b in zip([0] + cuts, cuts + [t.w])]
            parts = []
            for sz in sizes:
                if sz == 1 and rng.chance(60):
                    parts.append(self.expr(TLogic(), avail, d))
                else:
                    parts.append(self.expr(TBits(sz, t.signed, t.slv), avail, d))
            return Concat(t, parts)
        if k == "conv":
            kinds = [TBits(t.w, False, False), TBits(t.w, True, False),
                     TBits(t.w, False, True)]
            ot = rng.choice([x for x in kinds if x != t])
            return Conv(t, self.expr(ot, avail, d))
        return self.leaf(t, avail)

    def driver_expr(self, t, avail, depth):
        """Expression for a concurrent driver: it must read a name, since a
        driver without reads runs once and is no island candidate."""
        for _ in range(8):
            e = self.expr(t, avail, depth)
            if ir.expr_reads(e):
                return e
        return e

    def cond(self, avail, depth):
        return self.expr(TBool(), avail, max(depth, 1))

    # ----------------------------------------------------- statements
    def target(self, name, t):
        rng = self.rng
        if (self.sw["partial_target"] and isinstance(t, TBits) and t.w >= 2
                and rng.chance(15)):
            if rng.chance(40):
                return Index(TLogic(), Ref(t, name), rng.range(0, t.w - 1)), TLogic()
            w = rng.range(1, t.w - 1)
            lo = rng.range(0, t.w - w)
            st = TBits(w, t.signed, t.slv)
            return Slice(st, Ref(t, name), lo + w - 1, lo), st
        return Ref(t, name), t

    def assign(self, name, t, avail, depth, partial_ok=True):
        if partial_ok:
            tg, tt = self.target(name, t)
        else:
            tg, tt = Ref(t, name), t
        return Assign(tg, self.expr(tt, avail, depth))

    def case_sel(self, avail):
        sels = [(n, ty) for n, ty in avail
                if isinstance(ty, TLogic) or (isinstance(ty, TBits) and ty.w <= 4)]
        return self.rng.choice(sels) if sels else None

    def block(self, targets, avail, depth, sdepth):
        """Statements assigning some of targets [(name, type)]."""
        rng = self.rng
        out = []
        pending = rng.shuffle(list(targets))
        while pending:
            k = rng.range(1, len(pending))
            group, pending = pending[:k], pending[k:]
            opts = [("assign", 50)]
            if sdepth > 0:
                opts.append(("if", 30))
                if self.sw["case"] and self.case_sel(avail):
                    opts.append(("case", 20))
            kind = rng.weighted(opts)
            if kind == "assign":
                for n, t in group:
                    out.append(self.assign(n, t, avail, depth))
            elif kind == "if":
                arms = []
                for _ in range(rng.range(1, 3)):
                    sub = rng.sample(group, rng.range(1, len(group)))
                    arms.append(Arm(self.cond(avail, 2),
                                    self.block(sub, avail, depth, sdepth - 1)))
                els = None
                if rng.chance(50):
                    sub = rng.sample(group, rng.range(1, len(group)))
                    els = self.block(sub, avail, depth, sdepth - 1)
                out.append(If(arms, els))
            else:
                n, st = self.case_sel(avail)
                alts = [Alt(ch, self.block(rng.sample(group, rng.range(1, len(group))),
                                           avail, depth, sdepth - 1))
                        for ch in self.choice_sets(st)]
                others = []
                if rng.chance(60):
                    sub = rng.sample(group, rng.range(1, len(group)))
                    others = self.block(sub, avail, depth, sdepth - 1)
                out.append(Case(Ref(st, n), alts, others))
        return out

    def choice_sets(self, st):
        """1-3 disjoint lists of 1-2 choices for a selector of type st."""
        rng = self.rng
        w = 1 if isinstance(st, TLogic) else st.w
        space = rng.shuffle(["".join(x) for x in _all_bits(w)])
        out = []
        pos = 0
        for _ in range(rng.range(1, min(3, len(space)))):
            nch = min(rng.range(1, 2), len(space) - pos)
            if nch <= 0:
                break
            out.append(space[pos:pos + nch])
            pos += nch
        return out

    # -------------------------------------------------------- modules
    def new_signal(self, M, t, init=None):
        n = M.fresh("s")
        M.m.signals.append(Signal(n, t, init))
        return n

    def module(self, name, depth):
        rng = self.rng
        p = self.p
        M = _Mod(name)
        m = M.m
        m.ports.append(Port("clk", "in", TLogic()))
        if self.reset:
            m.ports.append(Port(self.reset[0], "in", TLogic()))
        for _ in range(rng.range(*p["n_in"])):
            pn = M.fresh("i")
            t = self.rtype()
            m.ports.append(Port(pn, "in", t))
            M.avail.append((pn, t))
        for _ in range(rng.range(*p["n_reg"])):
            t = self.rtype()
            init = None
            if not self.reset and rng.chance(p["init"]):
                init = self.const_value(t)
            elif self.reset and rng.chance(10):
                init = self.const_value(t)
            n = self.new_signal(M, t, init)
            M.regs.append((n, t))
            M.avail.append((n, t))

        steps = ["comb"] * rng.range(*p["n_comb"])
        children = []
        if depth < p["hier_depth"]:
            for _ in range(rng.range(*p["children"])):
                self.n_mod += 1
                cname = "m%d" % self.n_mod
                children.append(self.module(cname, depth + 1))
                steps.append(cname)
        rng.shuffle(steps)
        for st in steps:
            if st == "comb":
                self.comb_node(M)
            else:
                self.instance(M, next(c for c in children if c.name == st))
        self.ff_procs(M)
        self.outputs(M)
        if self.observe.monitors:
            cands = M.regs + M.combs
            for n, _ in rng.sample(cands, min(len(cands), rng.range(1, 4))):
                m.items.append(Monitor(M.fresh("mon"), n))
        rng.shuffle(m.items)
        self.modules.append(m)
        return m

    def comb_node(self, M):
        rng = self.rng
        p = self.p
        depth = p["depth"]
        opts = [("cassign", 60 - p["proc"]), ("proc", p["proc"])]
        if self.sw["ccond"]:
            opts.append(("ccond", 12))
        if self.sw["csel"] and self.case_sel(M.avail):
            opts.append(("csel", 10))
        kind = rng.weighted(opts)
        avail = list(M.avail)
        if kind == "proc":
            outs = [(None, self.rtype()) for _ in range(rng.range(1, 3))]
            outs = [(self.new_signal(M, t), t) for _, t in outs]
            for _ in range(8):
                body = [Assign(Ref(t, n), self.expr(t, avail, depth)) for n, t in outs]
                body += self.block(outs, avail, depth, 2)
                if ir.stmts_rw(body)[0]:
                    break
            sens = None
            if self.sw["explicit_sens"] and rng.chance(50):
                reads, _ = ir.stmts_rw(body)
                sens = reads
            if sens == []:
                sens = None
            M.m.items.append(Comb(M.fresh("p"), sens, [], body))
        else:
            t = self.rtype()
            n = self.new_signal(M, t)
            outs = [(n, t)]
            lab = M.fresh("a")
            if kind == "cassign":
                M.m.items.append(CAssign(lab, Ref(t, n), self.driver_expr(t, avail, depth)))
            elif kind == "ccond":
                arms = [CArm(self.cond(avail, 2), self.expr(t, avail, depth))
                        for _ in range(rng.range(1, 3))]
                M.m.items.append(CCond(lab, Ref(t, n), arms, self.expr(t, avail, depth)))
            else:
                sn, st = self.case_sel(avail)
                alts = [CAlt(ch, self.expr(t, avail, depth)) for ch in self.choice_sets(st)]
                M.m.items.append(CSel(lab, Ref(t, n), Ref(st, sn), alts,
                                      self.expr(t, avail, depth)))
        for n, t in outs:
            M.combs.append((n, t))
            M.avail.append((n, t))

    def instance(self, M, child):
        rng = self.rng
        ports = []
        outs = []
        for cp in child.ports:
            if cp.name == "clk" or (self.reset and cp.name == self.reset[0]):
                ports.append(PortMap(cp.name, Ref(TLogic(), cp.name)))
            elif cp.dir == "in":
                same = [n for n, ty in M.avail if ty == cp.t]
                if same and rng.chance(70):
                    actual = Ref(cp.t, rng.choice(same))
                else:
                    n = self.new_signal(M, cp.t)
                    M.m.items.append(CAssign(M.fresh("a"), Ref(cp.t, n),
                                             self.driver_expr(cp.t, list(M.avail),
                                                              self.p["depth"])))
                    M.combs.append((n, cp.t))
                    M.avail.append((n, cp.t))
                    actual = Ref(cp.t, n)
                ports.append(PortMap(cp.name, actual))
            else:
                n = self.new_signal(M, cp.t)
                outs.append((n, cp.t))
                ports.append(PortMap(cp.name, Ref(cp.t, n)))
        M.m.items.append(Inst(M.fresh("u"), child.name, ports))
        for n, t in outs:
            M.combs.append((n, t))
            M.avail.append((n, t))

    def ff_procs(self, M):
        rng = self.rng
        regs = rng.shuffle(list(M.regs))
        avail = list(M.avail)
        while regs:
            k = rng.range(1, min(3, len(regs)))
            group, regs = regs[:k], regs[k:]
            edge = "fall" if self.sw["fall_edge"] and rng.chance(30) else "rise"
            reset = None
            if self.reset:
                rbody = [Assign(Ref(t, n), self.const(t)) for n, t in group
                         if rng.chance(90)]
                reset = Reset(self.reset[0], self.reset[1], "sync", rbody)
            body = self.block(group, avail, self.p["depth"], 2)
            M.m.items.append(FF(M.fresh("p"), "clk", edge, reset, [], body))

    def outputs(self, M):
        rng = self.rng
        avail = list(M.avail)
        for _ in range(rng.range(*self.p["n_out"])):
            # prefer the type of a register or late signal so outputs
            # observe the design rather than fresh constants
            src = M.regs + M.combs
            if src and rng.chance(70):
                t = rng.choice(src)[1]
            else:
                t = self.rtype()
            n = M.fresh("o")
            M.m.ports.append(Port(n, "out", t))
            lab = M.fresh("a")
            if self.sw["ccond"] and rng.chance(25):
                arms = [CArm(self.cond(avail, 2), self.expr(t, avail, self.p["depth"]))]
                M.m.items.append(CCond(lab, Ref(t, n), arms,
                                       self.expr(t, avail, self.p["depth"])))
            else:
                M.m.items.append(CAssign(lab, Ref(t, n),
                                         self.driver_expr(t, avail, self.p["depth"])))

    # --------------------------------------------------------- design
    def generate(self):
        top = self.module("m0", 0)
        design = Design(top.name, self.modules)
        tb = tbgen.gen_tb(self.rng.fork("tb"), design, self.reset, self.observe,
                          self.p)
        case = FuzzCase(ir.SCHEMA, self.seed, self.tier, self.profile, [],
                        design, tb)
        case.features = features(case, self.sw)
        return case


def _all_bits(w):
    if w == 0:
        yield ""
        return
    for rest in _all_bits(w - 1):
        yield "0" + rest
        yield "1" + rest


def features(case, sw=None):
    """Sorted list of feature tags present in the case (for statistics)."""
    f = set()
    for m in case.design.modules:
        for it in ir.walk_items(m.items):
            f.add("item:" + it.KIND)
            if isinstance(it, Comb) and it.sens is not None:
                f.add("explicit_sens")
            if isinstance(it, FF):
                f.add("edge:" + it.edge)
                if it.reset is not None:
                    f.add("reset:" + it.reset.kind)
        for s in m.signals + m.ports:
            if isinstance(s.t, TBits) and s.t.w > 64:
                f.add("wide")
    if len(case.design.modules) > 1:
        f.add("hier")
    for i in case.tb.inputs:
        f.add("timing:" + i.timing)
    ob = case.tb.observe
    if ob.vcd:
        f.add("obs:vcd")
    if ob.monitors:
        f.add("obs:monitors")
    if sw:
        f.update("sw:" + k for k, v in sw.items() if v)
    return sorted(f)


def generate(seed, tier="V1", profile=None):
    """Generate one case; the result passes ir.check()."""
    return Generator(seed, tier, profile).generate()
