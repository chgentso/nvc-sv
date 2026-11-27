"""IR -> VHDL-2008 text.  Deterministic: the same IR gives the same bytes.

Every binary operation is parenthesised, vector literals and
concatenations are qualified (`unsigned'("0101")`), so no expression
depends on overload resolution from context.
"""

from . import ir
from .ir import (TLogic, TBits, TBool, TInt, TString, Ref, Const, Index, Slice,
                 Concat, Unop, Binop, Shift, Resize, Conv, Call, ToStr, Assign,
                 If, Case, For, Report, Assert, Null, CAssign, CCond, CSel, Comb,
                 FF, Inst, Monitor)

HEADER = ("library ieee;\n"
          "use ieee.std_logic_1164.all;\n"
          "use ieee.numeric_std.all;\n")

BINOPS = {
    "and": "and", "or": "or", "xor": "xor", "nand": "nand", "nor": "nor",
    "xnor": "xnor", "add": "+", "sub": "-", "mul": "*",
    "eq": "=", "ne": "/=", "lt": "<", "le": "<=", "gt": ">", "ge": ">=",
}

UNOPS = {
    "not": "not", "neg": "-", "red_and": "and", "red_or": "or",
    "red_xor": "xor", "red_nand": "nand", "red_nor": "nor", "red_xnor": "xnor",
}

SHIFTS = {"sll": "shift_left", "srl": "shift_right",
          "rol": "rotate_left", "ror": "rotate_right"}


def type_mark(t):
    if isinstance(t, TBits):
        return "std_logic_vector" if t.slv else ("signed" if t.signed else "unsigned")
    if isinstance(t, TLogic):
        return "std_logic"
    if isinstance(t, TBool):
        return "boolean"
    if isinstance(t, TString):
        return "string"
    if isinstance(t, TInt):
        return "integer"
    raise NotImplementedError("VHDL type %s is not in tier V1" % t.KIND)


def vtype(t):
    if isinstance(t, TBits):
        return "%s(%d downto 0)" % (type_mark(t), t.w - 1)
    if isinstance(t, TInt):
        return "integer range %d to %d" % (t.lo, t.hi)
    return type_mark(t)


def const(t, v):
    if isinstance(t, TLogic):
        # qualified: '0' = '1' alone is ambiguous (bit, character, ...)
        return "std_logic'('%s')" % v
    if isinstance(t, TBits):
        return "%s'(\"%s\")" % (type_mark(t), v)
    if isinstance(t, (TBool, TInt)):
        return v
    if isinstance(t, TString):
        return '"%s"' % v
    raise NotImplementedError("VHDL constant of %s" % t.KIND)


def choice(t, v):
    if isinstance(t, TLogic):
        return "'%s'" % v
    if isinstance(t, TBits):
        return '"%s"' % v
    return v


def expr(e):
    if isinstance(e, Ref):
        return e.name
    if isinstance(e, Const):
        return const(e.t, e.value)
    if isinstance(e, Index):
        if isinstance(e.idx, int):
            return "%s(%d)" % (expr(e.base), e.idx)
        if isinstance(e.idx.t, TBits):
            return "%s(to_integer(%s))" % (expr(e.base), expr(e.idx))
        return "%s(%s)" % (expr(e.base), expr(e.idx))
    if isinstance(e, Slice):
        return "%s(%d downto %d)" % (expr(e.base), e.hi, e.lo)
    if isinstance(e, Concat):
        body = " & ".join(expr(p) for p in e.parts)
        if isinstance(e.t, TString):
            return "(%s)" % body
        return "%s'(%s)" % (type_mark(e.t), body)
    if isinstance(e, Unop):
        return "(%s %s)" % (UNOPS[e.op], expr(e.a))
    if isinstance(e, Binop):
        return "(%s %s %s)" % (expr(e.a), BINOPS[e.op], expr(e.b))
    if isinstance(e, Shift):
        return "%s(%s, %d)" % (SHIFTS[e.op], expr(e.a), e.n)
    if isinstance(e, Resize):
        return "resize(%s, %d)" % (expr(e.a), e.t.w)
    if isinstance(e, Conv):
        return "%s(%s)" % (type_mark(e.t), expr(e.a))
    if isinstance(e, ToStr):
        at = e.a.t
        if e.fmt == "image" or isinstance(at, TLogic):
            return "%s'image(%s)" % (type_mark(at), expr(e.a))
        if e.fmt == "hex":
            return "to_hstring(%s)" % expr(e.a)
        return "to_string(%s)" % expr(e.a)
    if isinstance(e, Call):
        return "%s(%s)" % (e.func, ", ".join(expr(a) for a in e.args))
    raise NotImplementedError("VHDL expression %s" % getattr(e, "KIND", e))


class _W:
    def __init__(self):
        self.lines = []

    def __call__(self, ind, text):
        self.lines.append("  " * ind + text)

    def text(self):
        return "\n".join(self.lines) + "\n"


def stmts(w, ind, body):
    if not body:
        w(ind, "null;")
    for s in body:
        stmt(w, ind, s)


def stmt(w, ind, s):
    if isinstance(s, Assign):
        w(ind, "%s %s %s;" % (expr(s.target), ":=" if s.var else "<=", expr(s.value)))
    elif isinstance(s, If):
        for i, a in enumerate(s.arms):
            w(ind, "%s %s then" % ("if" if i == 0 else "elsif", expr(a.cond)))
            stmts(w, ind + 1, a.body)
        if s.els is not None:
            w(ind, "else")
            stmts(w, ind + 1, s.els)
        w(ind, "end if;")
    elif isinstance(s, Case):
        kw = "case?" if s.matching else "case"
        w(ind, "%s %s is" % (kw, expr(s.sel)))
        for a in s.alts:
            w(ind + 1, "when %s =>" % " | ".join(choice(s.sel.t, c) for c in a.choices))
            stmts(w, ind + 2, a.body)
        if s.others is not None:
            w(ind + 1, "when others =>")
            stmts(w, ind + 2, s.others)
        w(ind, "end %s;" % kw)
    elif isinstance(s, For):
        w(ind, "for %s in %d to %d loop" % (s.var, s.lo, s.hi))
        stmts(w, ind + 1, s.body)
        w(ind, "end loop;")
    elif isinstance(s, Report):
        w(ind, "report %s severity %s;" % (expr(s.msg), s.severity))
    elif isinstance(s, Assert):
        w(ind, "assert %s report %s severity %s;"
          % (expr(s.cond), expr(s.msg), s.severity))
    elif isinstance(s, Null):
        w(ind, "null;")
    else:
        raise NotImplementedError("VHDL statement %s" % getattr(s, "KIND", s))


def proc_vars(w, ind, vs):
    for v in vs:
        init = "" if v.init is None else " := " + const(v.t, v.init)
        w(ind, "variable %s : %s%s;" % (v.name, vtype(v.t), init))


def edge_fn(edge):
    return "rising_edge" if edge == "rise" else "falling_edge"


def item(w, ind, it, mod):
    if isinstance(it, CAssign):
        w(ind, "%s: %s <= %s;" % (it.label, expr(it.target), expr(it.value)))
    elif isinstance(it, CCond):
        w(ind, "%s: %s <=" % (it.label, expr(it.target)))
        for a in it.arms:
            w(ind + 1, "%s when %s else" % (expr(a.value), expr(a.cond)))
        w(ind + 1, "%s;" % expr(it.els))
    elif isinstance(it, CSel):
        w(ind, "%s: with %s select %s <=" % (it.label, expr(it.sel), expr(it.target)))
        for a in it.alts:
            w(ind + 1, "%s when %s," % (expr(a.value),
                                        " | ".join(choice(it.sel.t, c) for c in a.choices)))
        w(ind + 1, "%s when others;" % expr(it.others))
    elif isinstance(it, Comb):
        sens = "all" if it.sens is None else ", ".join(it.sens)
        w(ind, "%s: process (%s) is" % (it.label, sens))
        proc_vars(w, ind + 1, it.vars)
        w(ind, "begin")
        stmts(w, ind + 1, it.body)
        w(ind, "end process;")
    elif isinstance(it, FF):
        sens = [it.clock]
        if it.reset is not None and it.reset.kind == "async":
            sens.append(it.reset.signal)
        w(ind, "%s: process (%s) is" % (it.label, ", ".join(sens)))
        proc_vars(w, ind + 1, it.vars)
        w(ind, "begin")
        clk = "%s(%s)" % (edge_fn(it.edge), it.clock)
        r = it.reset
        if r is None:
            w(ind + 1, "if %s then" % clk)
            stmts(w, ind + 2, it.body)
            w(ind + 1, "end if;")
        elif r.kind == "async":
            w(ind + 1, "if %s = '%s' then" % (r.signal, r.level))
            stmts(w, ind + 2, r.body)
            w(ind + 1, "elsif %s then" % clk)
            stmts(w, ind + 2, it.body)
            w(ind + 1, "end if;")
        else:
            w(ind + 1, "if %s then" % clk)
            w(ind + 2, "if %s = '%s' then" % (r.signal, r.level))
            stmts(w, ind + 3, r.body)
            w(ind + 2, "else")
            stmts(w, ind + 3, it.body)
            w(ind + 2, "end if;")
            w(ind + 1, "end if;")
        w(ind, "end process;")
    elif isinstance(it, Inst):
        w(ind, "%s: entity work.%s" % (it.label, it.module))
        w(ind + 1, "port map (")
        maps = ["%s => %s" % (pm.formal, "open" if pm.actual is None else expr(pm.actual))
                for pm in it.ports]
        for i, s in enumerate(maps):
            w(ind + 2, s + ("," if i + 1 < len(maps) else ""))
        w(ind + 1, ");")
    elif isinstance(it, Monitor):
        t = _decl_type(mod, it.signal)
        v = ToStr(TString(), Ref(t, it.signal), "bin")
        w(ind, "%s: process (%s) is" % (it.label, it.signal))
        w(ind, "begin")
        w(ind + 1, "report \"FZ M %s.%s \" & %s;" % (mod.name, it.signal, expr(v)))
        w(ind, "end process;")
    else:
        raise NotImplementedError("VHDL item %s is not in tier V1"
                                  % getattr(it, "KIND", it))


def _decl_type(mod, name):
    for p in mod.ports:
        if p.name == name:
            return p.t
    for s in mod.signals:
        if s.name == name:
            return s.t
    raise KeyError(name)


def module(m):
    if m.funcs:
        raise NotImplementedError("VHDL functions are not in tier V1")
    w = _W()
    w(0, "-- generated by hdlfuzz")
    for line in HEADER.splitlines():
        w(0, line)
    w(0, "")
    w(0, "entity %s is" % m.name)
    if m.ports:
        w(1, "port (")
        for i, p in enumerate(m.ports):
            w(2, "%s : %s %s%s" % (p.name, p.dir, vtype(p.t),
                                   ";" if i + 1 < len(m.ports) else ""))
        w(1, ");")
    w(0, "end entity %s;" % m.name)
    w(0, "")
    w(0, "architecture rtl of %s is" % m.name)
    for s in m.signals:
        init = "" if s.init is None else " := " + const(s.t, s.init)
        w(1, "signal %s : %s%s;" % (s.name, vtype(s.t), init))
    w(0, "begin")
    for it in m.items:
        item(w, 1, it, m)
    w(0, "end architecture rtl;")
    return w.text()


def design(d):
    """All modules of the design in analysis order, as one text."""
    return "\n".join(module(m) for m in d.modules)


TB_NAME = "fz_tb"


def _value_expr(name, t):
    return expr(ToStr(TString(), Ref(t, name), "bin"))


def testbench(case):
    """The testbench entity fz_tb for case (see FORMAT.md)."""
    d = case.design
    tb = case.tb
    top = next(m for m in d.modules if m.name == d.top)
    n = tb.cycles
    inputs = {i.port: i for i in tb.inputs}
    outs = [p for p in top.ports if p.dir == "out"]
    w = _W()
    w(0, "-- generated by hdlfuzz")
    for line in HEADER.splitlines():
        w(0, line)
    w(0, "")
    w(0, "entity %s is" % TB_NAME)
    w(0, "end entity %s;" % TB_NAME)
    w(0, "")
    w(0, "architecture tb of %s is" % TB_NAME)
    w(1, "constant fz_period : time := %d ns;" % tb.period_ns)
    for p in top.ports:
        i = inputs.get(p.name)
        if i is None:
            continue
        w(1, "type fz_t_%s is array (0 to %d) of %s;" % (p.name, n - 1, vtype(p.t)))
        # element type is known from the array type: plain literals
        vals = [choice(p.t, v) for v in i.values]
        if n == 1:
            w(1, "constant fz_v_%s : fz_t_%s := (0 => %s);" % (p.name, p.name, vals[0]))
        else:
            per = max(1, 72 // (len(vals[0]) + 2))
            w(1, "constant fz_v_%s : fz_t_%s := (" % (p.name, p.name))
            for k in range(0, n, per):
                chunk = ", ".join(vals[k:k + per])
                w(2, chunk + ("," if k + per < n else ""))
            w(1, ");")
    for p in top.ports:
        init = ""
        if p.name == tb.clock:
            init = " := '0'"
        elif tb.reset is not None and p.name == tb.reset.port:
            init = " := '%s'" % tb.reset.level
        elif p.name in inputs and inputs[p.name].timing != "fall":
            init = " := fz_v_%s(0)" % p.name
        w(1, "signal %s : %s%s;" % (p.name, vtype(p.t), init))
        i = inputs.get(p.name)
        if i is not None and i.timing == "delta":
            for k in range(1, i.delta + 1):
                w(1, "signal fz_%s_d%d : %s := fz_v_%s(0);" % (p.name, k, vtype(p.t), p.name))
    w(0, "begin")
    w(1, "dut: entity work.%s" % top.name)
    w(2, "port map (")
    maps = []
    for p in top.ports:
        i = inputs.get(p.name)
        actual = p.name
        if i is not None and i.timing == "delta":
            actual = "fz_%s_d%d" % (p.name, i.delta)
        maps.append("%s => %s" % (p.name, actual))
    for k, s in enumerate(maps):
        w(3, s + ("," if k + 1 < len(maps) else ""))
    w(2, ");")
    w(0, "")
    # clock and end of test
    w(1, "fz_clock: process is")
    w(1, "begin")
    w(2, "for c in 0 to %d loop" % (n - 1))
    w(3, "wait for fz_period / 2;")
    w(3, "%s <= '1';" % tb.clock)
    w(3, "wait for fz_period / 2;")
    w(3, "%s <= '0';" % tb.clock)
    w(2, "end loop;")
    w(2, "wait for fz_period / 4;")
    if tb.observe.final:
        for p in outs:
            w(2, "report \"FZ F %s \" & %s;" % (p.name, _value_expr(p.name, p.t)))
    w(2, "report \"FZ END\";")
    w(2, "std.env.finish;")
    w(2, "wait;")
    w(1, "end process;")
    if tb.reset is not None:
        r = tb.reset
        w(0, "")
        w(1, "fz_reset: process is")
        w(1, "begin")
        w(2, "wait for %d * fz_period;" % r.cycles)
        w(2, "%s <= '%s';" % (r.port, "0" if r.level == "1" else "1"))
        w(2, "wait;")
        w(1, "end process;")
    for i in tb.inputs:
        w(0, "")
        w(1, "fz_in_%s: process is" % i.port)
        w(1, "begin")
        if i.timing != "fall":
            w(2, "wait for fz_period / 2;")
        w(2, "for c in 0 to %d loop" % (n - 1))
        w(3, "%s <= fz_v_%s(c);" % (i.port, i.port))
        w(3, "wait for fz_period;")
        w(2, "end loop;")
        w(2, "wait;")
        w(1, "end process;")
        for k in range(1, i.delta + 1):
            src = i.port if k == 1 else "fz_%s_d%d" % (i.port, k - 1)
            w(1, "fz_%s_d%d <= %s;" % (i.port, k, src))
    if tb.observe.sampler and outs:
        w(0, "")
        w(1, "fz_sample: process is")
        w(1, "begin")
        w(2, "wait for fz_period / 2 + fz_period / 4;")
        w(2, "for c in 0 to %d loop" % (n - 1))
        parts = ['"FZ S " & integer\'image(c)']
        for p in outs:
            parts.append('" %s=" & %s' % (p.name, _value_expr(p.name, p.t)))
        w(3, "report %s;" % " & ".join(parts))
        w(3, "wait for fz_period;")
        w(2, "end loop;")
        w(2, "wait;")
        w(1, "end process;")
    w(0, "end architecture tb;")
    return w.text()
