"""Typed IR of a generated case: types, expressions, statements, module
items, modules, the design and its testbench.

Every node is a dataclass with a JSON tag `k`; `to_json`/`from_json` give a
lossless round trip.  Expressions carry their result type in `t`, and
`check()` recomputes it.  The node set covers section 4 of the plan; nodes
that tier V1 does not generate yet (Int/Enum/Record/Array types, Ram,
generate, functions) are part of the schema and of `check()`, and the VHDL
printer refuses them with NotImplementedError.

The IR is language neutral where it can be: `Bits(w, signed, slv)` is a
vector of 4/9-valued bits (slv=True: std_logic_vector, no arithmetic),
arithmetic needs equal operand types and the generator adds explicit
`Resize`/`Conv` nodes, so the printers never guess a width.
"""

import dataclasses
import re
from dataclasses import dataclass, field
from typing import Any, List, Optional

SCHEMA = 1

_KINDS = {}


def node(kind):
    def wrap(cls):
        cls = dataclass(cls)
        cls.KIND = kind
        _KINDS[kind] = cls
        return cls
    return wrap


# ---------------------------------------------------------------- types

@node("logic")
class TLogic:
    """std_logic / logic scalar."""


@node("bits")
class TBits:
    w: int
    signed: bool = False
    slv: bool = False       # VHDL std_logic_vector (no arithmetic)


@node("bool")
class TBool:
    pass


@node("int")
class TInt:
    lo: int
    hi: int


@node("string")
class TString:
    pass


@node("enum")
class TEnum:
    name: str
    lits: List[str]


@node("field")
class Field:
    name: str
    t: Any


@node("record")
class TRecord:
    name: str
    fields: List[Field]


@node("array")
class TArray:
    name: str
    elem: Any
    depth: int


# ---------------------------------------------------------- expressions

@node("ref")
class Ref:
    t: Any
    name: str


@node("const")
class Const:
    """Literal.  logic: one char; bits: w chars MSB first over "01UXZWLH-";
    bool: "true"/"false"; int: decimal; string: the text."""
    t: Any
    value: str


@node("index")
class Index:
    t: Any
    base: Any               # Ref
    idx: Any                # int, or Expr of type Bits/Int (dynamic)


@node("slice")
class Slice:
    t: Any
    base: Any               # Ref
    hi: int
    lo: int


@node("concat")
class Concat:
    t: Any
    parts: List[Any]


@node("unop")
class Unop:
    t: Any
    op: str
    a: Any


@node("binop")
class Binop:
    t: Any
    op: str
    a: Any
    b: Any


@node("shift")
class Shift:
    t: Any
    op: str                 # sll srl rol ror (srl of signed is arithmetic)
    a: Any
    n: int


@node("resize")
class Resize:
    t: Any
    a: Any


@node("conv")
class Conv:
    t: Any
    a: Any


@node("call")
class Call:
    t: Any
    func: str
    args: List[Any]


@node("tostr")
class ToStr:
    t: Any
    a: Any
    fmt: str = "bin"        # bin hex image


# ----------------------------------------------------------- statements

@node("assign")
class Assign:
    target: Any             # Ref, Index(Ref, int), Slice(Ref)
    value: Any
    var: bool = False


@node("arm")
class Arm:
    cond: Any
    body: List[Any]


@node("if")
class If:
    arms: List[Arm]
    els: Optional[List[Any]] = None


@node("alt")
class Alt:
    choices: List[str]
    body: List[Any]


@node("case")
class Case:
    sel: Any                # Ref
    alts: List[Alt]
    others: Optional[List[Any]] = None
    matching: bool = False  # VHDL case?


@node("for")
class For:
    var: str
    lo: int
    hi: int
    body: List[Any]


@node("report")
class Report:
    msg: Any
    severity: str = "note"


@node("assert")
class Assert:
    cond: Any
    msg: Any
    severity: str = "error"


@node("null")
class Null:
    pass


# ---------------------------------------------------------- declarations

@node("port")
class Port:
    name: str
    dir: str                # in out
    t: Any


@node("signal")
class Signal:
    name: str
    t: Any
    init: Optional[str] = None


@node("var")
class Var:
    name: str
    t: Any
    init: Optional[str] = None


@node("param")
class Param:
    name: str
    t: Any


@node("func")
class Func:
    name: str
    params: List[Param]
    ret: Any                # None: procedure
    vars: List[Var]
    body: List[Any]


# ---------------------------------------------------------- module items

@node("cassign")
class CAssign:
    label: str
    target: Any
    value: Any


@node("carm")
class CArm:
    cond: Any
    value: Any


@node("ccond")
class CCond:
    label: str
    target: Any
    arms: List[CArm]
    els: Any


@node("calt")
class CAlt:
    choices: List[str]
    value: Any


@node("csel")
class CSel:
    label: str
    target: Any
    sel: Any                # Ref
    alts: List[CAlt]
    others: Any


@node("comb")
class Comb:
    label: str
    sens: Optional[List[str]]   # None: process (all)
    vars: List[Var]
    body: List[Any]
    partial: bool = False   # sensitivity list deliberately incomplete
    latch: bool = False     # default assignments deliberately missing


@node("reset")
class Reset:
    signal: str
    level: str              # "1" or "0"
    kind: str               # sync async
    body: List[Any]


@node("ff")
class FF:
    label: str
    clock: str
    edge: str               # rise fall
    reset: Optional[Reset]
    vars: List[Var]
    body: List[Any]


@node("ram")
class Ram:
    label: str
    name: str
    width: int
    depth: int
    clock: str
    we: str
    waddr: str
    wdata: str
    raddr: str
    rdata: str
    sync_read: bool = True


@node("portmap")
class PortMap:
    formal: str
    actual: Any             # Expr, or None for open


@node("inst")
class Inst:
    label: str
    module: str
    ports: List[PortMap]


@node("genfor")
class GenFor:
    label: str
    var: str
    lo: int
    hi: int
    items: List[Any]


@node("genif")
class GenIf:
    label: str
    cond: Any
    items: List[Any]


@node("monitor")
class Monitor:
    """Observer: reports every event on `signal` ("FZ M <module>.<signal> v")."""
    label: str
    signal: str


@node("module")
class Module:
    name: str
    ports: List[Port]
    signals: List[Signal]
    items: List[Any]
    funcs: List[Func] = field(default_factory=list)


@node("design")
class Design:
    top: str
    modules: List[Module]   # children before parents


# ------------------------------------------------------------ testbench

@node("tbreset")
class TbReset:
    port: str
    level: str              # active level "1"/"0"
    cycles: int


@node("input")
class Input:
    port: str
    timing: str             # fall rise delta
    delta: int              # delta stages after the rising edge (timing=delta)
    values: List[str]       # one Const value string per cycle


@node("observe")
class Observe:
    vcd: bool = True
    monitors: bool = True
    sampler: bool = True
    final: bool = True


@node("tb")
class Testbench:
    clock: str
    period_ns: int
    cycles: int
    reset: Optional[TbReset]
    inputs: List[Input]
    observe: Observe


@node("case_")
class FuzzCase:
    schema: int
    seed: int
    tier: str
    profile: str
    features: List[str]
    design: Design
    tb: Testbench


# --------------------------------------------------------- serialisation

def to_json(x):
    if dataclasses.is_dataclass(x) and not isinstance(x, type):
        d = {"k": x.KIND}
        for f in dataclasses.fields(x):
            d[f.name] = to_json(getattr(x, f.name))
        return d
    if isinstance(x, list):
        return [to_json(e) for e in x]
    if isinstance(x, dict):
        return {k: to_json(v) for k, v in x.items()}
    if x is None or isinstance(x, (bool, int, str)):
        return x
    raise TypeError("cannot serialise %r" % (x,))


def from_json(x):
    if isinstance(x, list):
        return [from_json(e) for e in x]
    if isinstance(x, dict):
        if "k" in x:
            cls = _KINDS.get(x["k"])
            if cls is None:
                raise ValueError("unknown IR node kind %r" % x["k"])
            names = {f.name for f in dataclasses.fields(cls)}
            extra = set(x) - names - {"k"}
            if extra:
                raise ValueError("unknown fields %s in %s" % (sorted(extra), x["k"]))
            return cls(**{k: from_json(v) for k, v in x.items() if k != "k"})
        return {k: from_json(v) for k, v in x.items()}
    return x


# ------------------------------------------------------------- helpers

LOGIC_CHARS = "01UXZWLH-"

VHDL_RESERVED = set("""
abs access after alias all and architecture array assert assume assume_guarantee
attribute begin block body buffer bus case component configuration constant
context cover default disconnect downto else elsif end entity exit fairness file
for force function generate generic group guarded if impure in inertial inout is
label library linkage literal loop map mod nand new next nor not null of on open
or others out package parameter port postponed private procedure process property
protected pure range record register reject release rem report restrict
restrict_guarantee return rol ror select sequence severity shared signal sla sll
sra srl strong subtype then to transport type unaffected units until use variable
view vmode vprop vunit wait when while with xnor xor
""".split())

SV_RESERVED = set("""
always always_comb always_ff always_latch and assign begin bit buf byte case
casex casez cell config const default disable do edge else end endcase
endfunction endgenerate endmodule endtask enum event final for force forever
fork function generate genvar if initial inout input int integer join logic
longint module nand negedge nor not or output parameter posedge priority real
reg release repeat return shortint signed struct supply0 supply1 task time tri
type typedef union unique unique0 unsigned var void wait while wire wor xnor xor
""".split())

_IDENT_RE = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")


def legal_ident(name):
    return (isinstance(name, str) and bool(_IDENT_RE.match(name))
            and name not in VHDL_RESERVED and name not in SV_RESERVED)


def is_num(t):
    return isinstance(t, TBits) and not t.slv


def expr_reads(e, out=None):
    """Signal/variable names read by expression e, in first-use order."""
    if out is None:
        out = []
    if isinstance(e, Ref):
        if e.name not in out:
            out.append(e.name)
    elif isinstance(e, (Index,)):
        expr_reads(e.base, out)
        if not isinstance(e.idx, int):
            expr_reads(e.idx, out)
    elif isinstance(e, Slice):
        expr_reads(e.base, out)
    elif isinstance(e, Concat):
        for p in e.parts:
            expr_reads(p, out)
    elif isinstance(e, Binop):
        expr_reads(e.a, out)
        expr_reads(e.b, out)
    elif isinstance(e, (Unop, Shift, Resize, Conv, ToStr)):
        expr_reads(e.a, out)
    elif isinstance(e, Call):
        for a in e.args:
            expr_reads(a, out)
    return out


def target_name(t):
    while isinstance(t, (Index, Slice)):
        t = t.base
    if not isinstance(t, Ref):
        raise ValueError("bad assignment target")
    return t.name


def target_reads(t, out):
    """Names read by the index expression of a target (not the target)."""
    if isinstance(t, Index) and not isinstance(t.idx, int):
        expr_reads(t.idx, out)


def stmts_rw(stmts, reads=None, writes=None):
    """(reads, writes) of a statement list, in first-use order."""
    if reads is None:
        reads, writes = [], []
    for s in stmts:
        if isinstance(s, Assign):
            expr_reads(s.value, reads)
            target_reads(s.target, reads)
            n = target_name(s.target)
            if not s.var and n not in writes:
                writes.append(n)
        elif isinstance(s, If):
            for a in s.arms:
                expr_reads(a.cond, reads)
                stmts_rw(a.body, reads, writes)
            if s.els is not None:
                stmts_rw(s.els, reads, writes)
        elif isinstance(s, Case):
            expr_reads(s.sel, reads)
            for a in s.alts:
                stmts_rw(a.body, reads, writes)
            if s.others is not None:
                stmts_rw(s.others, reads, writes)
        elif isinstance(s, For):
            stmts_rw(s.body, reads, writes)
        elif isinstance(s, Report):
            expr_reads(s.msg, reads)
        elif isinstance(s, Assert):
            expr_reads(s.cond, reads)
            expr_reads(s.msg, reads)
    return reads, writes


def item_rw(it):
    """(reads, writes) of a module item; variables are filtered out."""
    if isinstance(it, CAssign):
        r = expr_reads(it.value)
        target_reads(it.target, r)
        return r, [target_name(it.target)]
    if isinstance(it, CCond):
        r = []
        for a in it.arms:
            expr_reads(a.cond, r)
            expr_reads(a.value, r)
        expr_reads(it.els, r)
        target_reads(it.target, r)
        return r, [target_name(it.target)]
    if isinstance(it, CSel):
        r = expr_reads(it.sel)
        for a in it.alts:
            expr_reads(a.value, r)
        expr_reads(it.others, r)
        target_reads(it.target, r)
        return r, [target_name(it.target)]
    if isinstance(it, (Comb, FF)):
        r, w = stmts_rw(it.body)
        if isinstance(it, FF):
            r = [it.clock] + [x for x in r if x != it.clock]
            if it.reset is not None:
                r2, w2 = stmts_rw(it.reset.body)
                if it.reset.signal not in r:
                    r.append(it.reset.signal)
                r += [x for x in r2 if x not in r]
                w += [x for x in w2 if x not in w]
        vs = {v.name for v in it.vars}
        loops = set()
        _loop_vars(it.body, loops)
        drop = vs | loops
        return [x for x in r if x not in drop], [x for x in w if x not in drop]
    if isinstance(it, Monitor):
        return [it.signal], []
    if isinstance(it, Ram):
        return [it.clock, it.we, it.waddr, it.wdata, it.raddr], [it.rdata]
    return [], []


def _loop_vars(stmts, out):
    for s in stmts:
        if isinstance(s, For):
            out.add(s.var)
            _loop_vars(s.body, out)
        elif isinstance(s, If):
            for a in s.arms:
                _loop_vars(a.body, out)
            if s.els:
                _loop_vars(s.els, out)
        elif isinstance(s, Case):
            for a in s.alts:
                _loop_vars(a.body, out)
            if s.others:
                _loop_vars(s.others, out)


def walk_items(items):
    """Yield items, descending into generate bodies."""
    for it in items:
        yield it
        if isinstance(it, (GenFor, GenIf)):
            yield from walk_items(it.items)


# ---------------------------------------------------------------- check

class IRError(Exception):
    pass


class _Checker:
    def __init__(self):
        self.errs = []

    def err(self, where, msg):
        self.errs.append("%s: %s" % (where, msg))

    # types ---------------------------------------------------------
    def check_type(self, where, t):
        if isinstance(t, TBits):
            if not isinstance(t.w, int) or not 1 <= t.w <= 4096:
                self.err(where, "bad width %r" % (t.w,))
            if t.slv and t.signed:
                self.err(where, "std_logic_vector cannot be signed")
        elif isinstance(t, TInt):
            if t.lo > t.hi:
                self.err(where, "empty integer range")
        elif isinstance(t, TEnum):
            if not t.lits or len(set(t.lits)) != len(t.lits):
                self.err(where, "bad enumeration literals")
        elif isinstance(t, TRecord):
            if not t.fields:
                self.err(where, "empty record")
            for f in t.fields:
                self.check_type(where, f.t)
        elif isinstance(t, TArray):
            if t.depth < 1:
                self.err(where, "bad array depth")
            self.check_type(where, t.elem)
        elif not isinstance(t, (TLogic, TBool, TString)):
            self.err(where, "not a type: %r" % (t,))

    def check_value(self, where, t, v):
        if v is None:
            return
        if not isinstance(v, str):
            self.err(where, "constant value must be a string")
        elif isinstance(t, TLogic):
            if len(v) != 1 or v not in LOGIC_CHARS:
                self.err(where, "bad logic constant %r" % v)
        elif isinstance(t, TBits):
            if len(v) != t.w or any(c not in LOGIC_CHARS for c in v):
                self.err(where, "bad %d-bit constant %r" % (t.w, v))
        elif isinstance(t, TBool):
            if v not in ("true", "false"):
                self.err(where, "bad boolean constant %r" % v)
        elif isinstance(t, TInt):
            if not re.match(r"^-?[0-9]+$", v) or not t.lo <= int(v) <= t.hi:
                self.err(where, "bad integer constant %r" % v)
        elif isinstance(t, TEnum):
            if v not in t.lits:
                self.err(where, "bad enumeration constant %r" % v)
        elif isinstance(t, TString):
            if '"' in v or "\n" in v:
                self.err(where, "bad string constant")
        else:
            self.err(where, "no constants of this type")

    # expressions ---------------------------------------------------
    def expr(self, where, e, env):
        """Check e and return its computed type (None on error)."""
        t = self._expr(where, e, env)
        if t is not None and getattr(e, "t", None) != t:
            self.err(where, "%s: stored type %r differs from %r"
                     % (getattr(e, "KIND", "?"), getattr(e, "t", None), t))
            return None
        return t

    def _expr(self, where, e, env):
        if isinstance(e, Ref):
            t = env.get(e.name)
            if t is None:
                self.err(where, "unknown name %r" % e.name)
            return t
        if isinstance(e, Const):
            self.check_type(where, e.t)
            self.check_value(where, e.t, e.value)
            return e.t
        if isinstance(e, Index):
            if not isinstance(e.base, Ref):
                self.err(where, "index base must be a name")
                return None
            bt = self.expr(where, e.base, env)
            if isinstance(bt, TBits):
                if isinstance(e.idx, int):
                    if not 0 <= e.idx < bt.w:
                        self.err(where, "index %d out of range" % e.idx)
                else:
                    it = self.expr(where, e.idx, env)
                    if isinstance(it, TBits):
                        if not it.signed is False or it.slv:
                            self.err(where, "dynamic index must be unsigned")
                        elif (1 << it.w) > bt.w:
                            self.err(where, "dynamic index can leave the range")
                    elif isinstance(it, TInt):
                        if it.lo < 0 or it.hi >= bt.w:
                            self.err(where, "integer index can leave the range")
                    else:
                        self.err(where, "bad index type")
                return TLogic()
            if isinstance(bt, TArray):
                return bt.elem
            self.err(where, "indexing a non-array")
            return None
        if isinstance(e, Slice):
            if not isinstance(e.base, Ref):
                self.err(where, "slice base must be a name")
                return None
            bt = self.expr(where, e.base, env)
            if not isinstance(bt, TBits):
                self.err(where, "slicing a non-vector")
                return None
            if not (0 <= e.lo <= e.hi < bt.w):
                self.err(where, "slice %d downto %d out of range" % (e.hi, e.lo))
                return None
            return TBits(e.hi - e.lo + 1, bt.signed, bt.slv)
        if isinstance(e, Concat):
            if isinstance(e.t, TString):
                for p in e.parts:
                    if not isinstance(self.expr(where, p, env), TString):
                        self.err(where, "string concatenation of a non-string")
                return TString() if e.parts else None
            if not isinstance(e.t, TBits) or len(e.parts) < 2:
                self.err(where, "concatenation needs a vector type and 2+ parts")
                return None
            w = 0
            for p in e.parts:
                pt = self.expr(where, p, env)
                if isinstance(pt, TLogic):
                    w += 1
                elif (isinstance(pt, TBits) and pt.signed == e.t.signed
                      and pt.slv == e.t.slv):
                    w += pt.w
                elif pt is not None:
                    self.err(where, "concatenation part of another kind")
                    return None
                else:
                    return None
            return TBits(w, e.t.signed, e.t.slv)
        if isinstance(e, Unop):
            at = self.expr(where, e.a, env)
            if at is None:
                return None
            if e.op == "not":
                if isinstance(at, (TLogic, TBits, TBool)):
                    return at
            elif e.op == "neg":
                if isinstance(at, TBits) and at.signed:
                    return at
            elif e.op in ("red_and", "red_or", "red_xor",
                          "red_nand", "red_nor", "red_xnor"):
                if isinstance(at, TBits):
                    return TLogic()
            else:
                self.err(where, "unknown unary operator %r" % e.op)
                return None
            self.err(where, "operator %s on %r" % (e.op, at))
            return None
        if isinstance(e, Binop):
            at = self.expr(where, e.a, env)
            bt = self.expr(where, e.b, env)
            if at is None or bt is None:
                return None
            op = e.op
            if op in ("and", "or", "xor", "nand", "nor", "xnor"):
                if at == bt and isinstance(at, (TLogic, TBits, TBool)):
                    if isinstance(at, TBool) and op not in ("and", "or", "xor"):
                        self.err(where, "boolean %s not allowed" % op)
                        return None
                    return at
            elif op in ("add", "sub"):
                if at == bt and is_num(at):
                    return at
            elif op == "mul":
                if is_num(at) and is_num(bt) and at.signed == bt.signed:
                    return TBits(at.w + bt.w, at.signed)
            elif op in ("eq", "ne"):
                if at == bt and isinstance(at, (TLogic, TBits, TBool, TInt, TEnum)):
                    return TBool()
            elif op in ("lt", "le", "gt", "ge"):
                if at == bt and (is_num(at) or isinstance(at, TInt)):
                    return TBool()
            else:
                self.err(where, "unknown binary operator %r" % op)
                return None
            self.err(where, "operator %s on %r, %r" % (op, at, bt))
            return None
        if isinstance(e, Shift):
            at = self.expr(where, e.a, env)
            if at is None:
                return None
            if e.op not in ("sll", "srl", "rol", "ror"):
                self.err(where, "unknown shift %r" % e.op)
                return None
            if not is_num(at) or not isinstance(e.n, int) or e.n < 0:
                self.err(where, "bad shift operands")
                return None
            return at
        if isinstance(e, Resize):
            at = self.expr(where, e.a, env)
            if at is None:
                return None
            if not (is_num(at) and is_num(e.t) and at.signed == e.t.signed):
                self.err(where, "resize needs unsigned/signed of one kind")
                return None
            self.check_type(where, e.t)
            return e.t
        if isinstance(e, Conv):
            at = self.expr(where, e.a, env)
            if at is None:
                return None
            if (isinstance(at, TBits) and isinstance(e.t, TBits)
                    and at.w == e.t.w and at != e.t):
                self.check_type(where, e.t)
                return e.t
            self.err(where, "conversion needs two vector kinds of one width")
            return None
        if isinstance(e, ToStr):
            at = self.expr(where, e.a, env)
            if at is None:
                return None
            if e.fmt not in ("bin", "hex", "image"):
                self.err(where, "bad string format %r" % e.fmt)
            if e.fmt == "hex" and not isinstance(at, TBits):
                self.err(where, "hex string of a non-vector")
            return TString()
        if isinstance(e, Call):
            f = self.funcs.get(e.func)
            if f is None or f.ret is None:
                self.err(where, "unknown function %r" % e.func)
                return None
            if len(e.args) != len(f.params):
                self.err(where, "wrong argument count for %s" % e.func)
                return None
            for a, p in zip(e.args, f.params):
                if self.expr(where, a, env) != p.t:
                    self.err(where, "argument type mismatch for %s" % e.func)
            return f.ret
        self.err(where, "not an expression: %r" % (e,))
        return None

    def bool_expr(self, where, e, env):
        t = self.expr(where, e, env)
        if t is not None and not isinstance(t, TBool):
            self.err(where, "condition is not boolean")

    def choices(self, where, selt, choices, seen):
        for c in choices:
            self.check_value(where, selt, c)
            if c in seen:
                self.err(where, "duplicate choice %r" % c)
            seen.add(c)

    def choice_count(self, t):
        if isinstance(t, TLogic):
            return 9
        if isinstance(t, TBits):
            return 9 ** t.w
        if isinstance(t, TBool):
            return 2
        if isinstance(t, TEnum):
            return len(t.lits)
        if isinstance(t, TInt):
            return t.hi - t.lo + 1
        return None

    # statements ----------------------------------------------------
    def target(self, where, tg, env, writable, var):
        if isinstance(tg, Ref):
            if tg.name not in env:
                self.err(where, "unknown target %r" % tg.name)
                return None
        elif isinstance(tg, (Index, Slice)):
            if not isinstance(tg.base, Ref):
                self.err(where, "target base must be a name")
                return None
        else:
            self.err(where, "bad assignment target")
            return None
        n = target_name(tg)
        if n not in writable:
            self.err(where, "%r is not writable here" % n)
        elif writable[n] != var:
            self.err(where, "%s assignment to %r"
                     % ("variable" if var else "signal", n))
        return self.expr(where, tg, env)

    def stmts(self, where, stmts, env, writable):
        for s in stmts:
            self.stmt(where, s, env, writable)

    def stmt(self, where, s, env, writable):
        if isinstance(s, Assign):
            tt = self.target(where, s.target, env, writable, s.var)
            vt = self.expr(where, s.value, env)
            if tt is not None and vt is not None and tt != vt:
                self.err(where, "assignment of %r to %r" % (vt, tt))
        elif isinstance(s, If):
            if not s.arms:
                self.err(where, "if without arms")
            for a in s.arms:
                self.bool_expr(where, a.cond, env)
                self.stmts(where, a.body, env, writable)
            if s.els is not None:
                self.stmts(where, s.els, env, writable)
        elif isinstance(s, Case):
            if not isinstance(s.sel, Ref):
                self.err(where, "case selector must be a name")
                return
            st = self.expr(where, s.sel, env)
            if st is None:
                return
            seen = set()
            for a in s.alts:
                if not a.choices:
                    self.err(where, "case alternative without choices")
                self.choices(where, st, a.choices, seen)
                self.stmts(where, a.body, env, writable)
            if s.others is not None:
                self.stmts(where, s.others, env, writable)
            elif self.choice_count(st) != len(seen):
                self.err(where, "case without others does not cover all values")
        elif isinstance(s, For):
            if s.lo > s.hi:
                self.err(where, "empty for loop")
            if s.var in env:
                self.err(where, "loop variable %r hides a name" % s.var)
            env2 = dict(env)
            env2[s.var] = TInt(s.lo, s.hi)
            self.stmts(where, s.body, env2, writable)
        elif isinstance(s, Report):
            if not isinstance(self.expr(where, s.msg, env), TString):
                self.err(where, "report message is not a string")
            if s.severity not in ("note", "warning", "error", "failure"):
                self.err(where, "bad severity")
        elif isinstance(s, Assert):
            self.bool_expr(where, s.cond, env)
            if not isinstance(self.expr(where, s.msg, env), TString):
                self.err(where, "assert message is not a string")
            if s.severity not in ("note", "warning", "error", "failure"):
                self.err(where, "bad severity")
        elif isinstance(s, Null):
            pass
        else:
            self.err(where, "not a statement: %r" % (s,))

    def vars(self, where, vs, env, writable, names):
        for v in vs:
            self.name(where, v.name, names)
            self.check_type(where, v.t)
            self.check_value(where, v.t, v.init)
            env[v.name] = v.t
            writable[v.name] = True

    def name(self, where, n, names):
        if not legal_ident(n):
            self.err(where, "illegal identifier %r" % (n,))
        if n in names:
            self.err(where, "duplicate name %r" % (n,))
        names.add(n)

    # module --------------------------------------------------------
    def module(self, m, mods, allow_loops):
        where = "module %s" % m.name
        names = set()
        env = {}
        kinds = {}
        self.funcs = {f.name: f for f in m.funcs}
        for p in m.ports:
            self.name(where, p.name, names)
            self.check_type(where, p.t)
            if p.dir not in ("in", "out"):
                self.err(where, "bad port direction %r" % p.dir)
            env[p.name] = p.t
            kinds[p.name] = p.dir
        for s in m.signals:
            self.name(where, s.name, names)
            self.check_type(where, s.t)
            self.check_value(where, s.t, s.init)
            env[s.name] = s.t
            kinds[s.name] = "sig"
        for f in m.funcs:
            self.name(where, f.name, names)

        sig_writable = {n: False for n, k in kinds.items() if k != "in"}
        drivers = {}
        comb_edges = []         # (reader set, written names) of comb items
        mon_count = {}

        for it in walk_items(m.items):
            lab = getattr(it, "label", None)
            if lab is not None:
                self.name(where, lab, names)
            w2 = "%s %s" % (where, lab)
            if isinstance(it, CAssign):
                tt = self.target(w2, it.target, env, sig_writable, False)
                vt = self.expr(w2, it.value, env)
                if tt is not None and vt is not None and tt != vt:
                    self.err(w2, "assignment of %r to %r" % (vt, tt))
            elif isinstance(it, CCond):
                tt = self.target(w2, it.target, env, sig_writable, False)
                if not it.arms:
                    self.err(w2, "conditional assignment without arms")
                for a in it.arms:
                    self.bool_expr(w2, a.cond, env)
                    vt = self.expr(w2, a.value, env)
                    if tt is not None and vt is not None and tt != vt:
                        self.err(w2, "value of %r for %r" % (vt, tt))
                vt = self.expr(w2, it.els, env)
                if tt is not None and vt is not None and tt != vt:
                    self.err(w2, "value of %r for %r" % (vt, tt))
            elif isinstance(it, CSel):
                tt = self.target(w2, it.target, env, sig_writable, False)
                if not isinstance(it.sel, Ref):
                    self.err(w2, "selector must be a name")
                    continue
                st = self.expr(w2, it.sel, env)
                seen = set()
                for a in it.alts:
                    if not a.choices:
                        self.err(w2, "alternative without choices")
                    if st is not None:
                        self.choices(w2, st, a.choices, seen)
                    vt = self.expr(w2, a.value, env)
                    if tt is not None and vt is not None and tt != vt:
                        self.err(w2, "value of %r for %r" % (vt, tt))
                vt = self.expr(w2, it.others, env)
                if tt is not None and vt is not None and tt != vt:
                    self.err(w2, "value of %r for %r" % (vt, tt))
            elif isinstance(it, (Comb, FF)):
                env2 = dict(env)
                wr = dict(sig_writable)
                self.vars(w2, it.vars, env2, wr, set(names))
                if isinstance(it, FF):
                    for n in (it.clock,) + ((it.reset.signal,) if it.reset else ()):
                        if not isinstance(env.get(n), TLogic):
                            self.err(w2, "clock/reset %r is not a logic name" % n)
                    if it.edge not in ("rise", "fall"):
                        self.err(w2, "bad edge %r" % it.edge)
                    if it.reset is not None:
                        if it.reset.level not in ("0", "1"):
                            self.err(w2, "bad reset level")
                        if it.reset.kind not in ("sync", "async"):
                            self.err(w2, "bad reset kind")
                        self.stmts(w2, it.reset.body, env2, wr)
                self.stmts(w2, it.body, env2, wr)
                if isinstance(it, Comb):
                    self.comb_rules(w2, it)
            elif isinstance(it, Inst):
                self.inst(w2, it, mods, env, sig_writable)
            elif isinstance(it, Monitor):
                if it.signal not in env:
                    self.err(w2, "monitor of unknown name %r" % it.signal)
                mon_count[it.signal] = mon_count.get(it.signal, 0) + 1
            elif isinstance(it, Ram):
                self.err(w2, "ram items are not generated yet (tier V2)")
            elif isinstance(it, (GenFor, GenIf)):
                self.err(w2, "generate items are not generated yet (tier V2)")
            else:
                self.err(where, "not a module item: %r" % (it,))

            r, w = item_rw(it)
            if isinstance(it, Inst):
                r, w = self.inst_rw(it, mods)
            for n in w:
                if kinds.get(n) == "in":
                    self.err(w2, "drives input port %r" % n)
                drivers.setdefault(n, []).append(lab)
            if not isinstance(it, (FF, Monitor)):
                comb_edges.append((r, w))

        for n, labs in drivers.items():
            if len(labs) > 1:
                self.err(where, "%r has several drivers: %s" % (n, ", ".join(labs)))
        for p in m.ports:
            if p.dir == "out" and p.name not in drivers:
                self.err(where, "output %r is not driven" % p.name)
        for n, c in mon_count.items():
            if c > 1:
                self.err(where, "%r monitored twice" % n)
        if not allow_loops:
            self.loops(where, comb_edges)

    def comb_rules(self, where, it):
        reads, writes = stmts_rw(it.body)
        local = {v.name for v in it.vars}
        loops = set()
        _loop_vars(it.body, loops)
        sig_reads = [n for n in reads if n not in local and n not in loops]
        if it.sens is not None:
            if len(set(it.sens)) != len(it.sens):
                self.err(where, "duplicate name in sensitivity list")
            missing = [n for n in sig_reads if n not in it.sens]
            if missing and not it.partial:
                self.err(where, "sensitivity list misses %s" % ", ".join(missing))
            if not missing and it.partial:
                self.err(where, "partial flag on a complete sensitivity list")
        elif it.partial:
            self.err(where, "partial flag with process (all)")
        if any(n in writes for n in sig_reads):
            self.err(where, "combinational process reads a signal it drives")
        if not it.latch:
            first = set()
            for s in it.body:
                if isinstance(s, Assign) and not s.var:
                    first.add(target_name(s.target))
                else:
                    break
            missing = [n for n in writes if n not in first]
            if missing:
                self.err(where, "no default assignment for %s" % ", ".join(missing))

    def inst_rw(self, it, mods):
        child = mods.get(it.module)
        if child is None:
            return [], []
        dirs = {p.name: p.dir for p in child.ports}
        r, w = [], []
        for pm in it.ports:
            if pm.actual is None:
                continue
            if dirs.get(pm.formal) == "in":
                expr_reads(pm.actual, r)
            elif dirs.get(pm.formal) == "out":
                w.append(target_name(pm.actual))
        return r, w

    def inst(self, where, it, mods, env, writable):
        child = mods.get(it.module)
        if child is None:
            self.err(where, "unknown or later module %r" % it.module)
            return
        ports = {p.name: p for p in child.ports}
        seen = set()
        for pm in it.ports:
            p = ports.get(pm.formal)
            if p is None:
                self.err(where, "no port %r in %s" % (pm.formal, it.module))
                continue
            if pm.formal in seen:
                self.err(where, "port %r mapped twice" % pm.formal)
            seen.add(pm.formal)
            if pm.actual is None:
                if p.dir == "in":
                    self.err(where, "input %r left open" % pm.formal)
                continue
            if p.dir == "out":
                at = self.target(where, pm.actual, env, writable, False)
            else:
                at = self.expr(where, pm.actual, env)
            if at is not None and at != p.t:
                self.err(where, "actual of %r has type %r, port %r"
                         % (pm.formal, at, p.t))
        for p in child.ports:
            if p.dir == "in" and p.name not in seen:
                self.err(where, "input %r not mapped" % p.name)

    def loops(self, where, edges):
        """No cycle through combinational items (registers break cycles)."""
        succ = {}
        for r, w in edges:
            for a in r:
                for b in w:
                    succ.setdefault(a, set()).add(b)
        WHITE, GREY, BLACK = 0, 1, 2
        colour = {}
        for start in sorted(succ):
            if colour.get(start, WHITE) != WHITE:
                continue
            stack = [(start, iter(sorted(succ.get(start, ()))))]
            colour[start] = GREY
            while stack:
                n, it = stack[-1]
                nxt = next(it, None)
                if nxt is None:
                    colour[n] = BLACK
                    stack.pop()
                    continue
                c = colour.get(nxt, WHITE)
                if c == GREY:
                    self.err(where, "combinational loop through %r" % nxt)
                    return
                if c == WHITE:
                    colour[nxt] = GREY
                    stack.append((nxt, iter(sorted(succ.get(nxt, ())))))

    # design --------------------------------------------------------
    def design(self, d, allow_loops):
        mods = {}
        for m in d.modules:
            if not isinstance(m, Module):
                self.err("design", "not a module: %r" % (m,))
                continue
            if not legal_ident(m.name):
                self.err("design", "illegal module name %r" % m.name)
            if m.name in mods:
                self.err("design", "duplicate module %r" % m.name)
            # only earlier modules are visible: children before parents,
            # which also rules out recursive instantiation
            self.module(m, mods, allow_loops)
            mods[m.name] = m
        if d.top not in mods:
            self.err("design", "top %r not found" % d.top)
        return mods

    def testbench(self, tb, mods, top):
        where = "tb"
        m = mods.get(top)
        if m is None:
            return
        ports = {p.name: p for p in m.ports}
        if tb.period_ns < 4 or tb.period_ns % 4:
            self.err(where, "period must be a positive multiple of 4 ns")
        if tb.cycles < 1:
            self.err(where, "no cycles")
        clk = ports.get(tb.clock)
        if clk is None or clk.dir != "in" or not isinstance(clk.t, TLogic):
            self.err(where, "clock %r is not a logic input of the top" % tb.clock)
        used = {tb.clock}
        if tb.reset is not None:
            rp = ports.get(tb.reset.port)
            if rp is None or rp.dir != "in" or not isinstance(rp.t, TLogic):
                self.err(where, "reset %r is not a logic input" % tb.reset.port)
            if tb.reset.level not in ("0", "1"):
                self.err(where, "bad reset level")
            if not 0 < tb.reset.cycles < tb.cycles:
                self.err(where, "bad reset length")
            used.add(tb.reset.port)
        for inp in tb.inputs:
            p = ports.get(inp.port)
            if p is None or p.dir != "in":
                self.err(where, "%r is not an input of the top" % inp.port)
                continue
            if inp.port in used:
                self.err(where, "input %r driven twice" % inp.port)
            used.add(inp.port)
            if inp.timing not in ("fall", "rise", "delta"):
                self.err(where, "bad input timing %r" % inp.timing)
            if (inp.timing == "delta") != (inp.delta > 0) or inp.delta > 16:
                self.err(where, "bad delta count %r" % inp.delta)
            if len(inp.values) != tb.cycles:
                self.err(where, "%r needs one value per cycle" % inp.port)
            for v in inp.values:
                self.check_value(where, p.t, v)
        for p in m.ports:
            if p.dir == "in" and p.name not in used:
                self.err(where, "top input %r not driven" % p.name)
        mon = any(isinstance(it, Monitor) for mm in mods.values()
                  for it in walk_items(mm.items))
        if mon and not tb.observe.monitors:
            self.err(where, "monitor items present but observe.monitors is off")


def errors(x, allow_loops=False):
    """List of problems of a FuzzCase or Design (empty when legal)."""
    c = _Checker()
    try:
        if isinstance(x, FuzzCase):
            if x.schema != SCHEMA:
                c.err("case", "schema %r, expected %d" % (x.schema, SCHEMA))
            mods = c.design(x.design, allow_loops)
            if isinstance(x.tb, Testbench):
                c.testbench(x.tb, mods, x.design.top)
            else:
                c.err("case", "no testbench")
        elif isinstance(x, Design):
            c.design(x, allow_loops)
        else:
            c.err("case", "not a case or design")
    except (AttributeError, TypeError, ValueError) as e:
        # malformed nodes (wrong field types) are illegal IR, not a crash
        c.err("case", "malformed IR: %s: %s" % (type(e).__name__, e))
    return c.errs


def check(x, allow_loops=False):
    """Raise IRError if x is not legal IR."""
    errs = errors(x, allow_loops)
    if errs:
        raise IRError("\n".join(errs))
