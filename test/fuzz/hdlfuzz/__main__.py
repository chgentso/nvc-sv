"""python3 -m hdlfuzz <command> ...

gen     generate case directories from seeds
emit    re-emit a case directory from its case.json (round trip)
check   run ir.check() on case.json files
"""

import argparse
import os
import sys

from . import casefile, gen, ir, runcli
from .prng import case_seed


def cmd_gen(args):
    lo, _, hi = args.cases.partition("..")
    lo = int(lo)
    hi = int(hi) if hi else lo
    for k in range(lo, hi + 1):
        seed = case_seed(args.campaign, k) if args.campaign is not None else k
        case = gen.generate(seed, args.tier, args.profile)
        out = os.path.join(args.out, "c%06d" % k) if hi > lo or args.subdirs else args.out
        casefile.write_case(case, out)
        if args.verbose:
            print("%s seed=%d profile=%s" % (out, seed, case.profile))
    return 0


def cmd_emit(args):
    case = casefile.load_case(args.case)
    casefile.write_case(case, args.out)
    return 0


def cmd_check(args):
    bad = 0
    for p in args.cases:
        errs = ir.errors(casefile.load_case(p))
        for e in errs:
            print("%s: %s" % (p, e))
        bad += bool(errs)
    return 1 if bad else 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hdlfuzz")
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gen", help="generate case directories")
    g.add_argument("--cases", default="0",
                   help="case number or range A..B (seed = case number unless --campaign)")
    g.add_argument("--campaign", type=int, default=None,
                   help="campaign seed: seed of case k is case_seed(campaign, k)")
    g.add_argument("--tier", default="V1")
    g.add_argument("--profile", default=None, choices=sorted(gen.PROFILES))
    g.add_argument("--out", required=True)
    g.add_argument("--subdirs", action="store_true",
                   help="write cNNNNNN/ subdirectories even for one case")
    g.add_argument("-v", "--verbose", action="store_true")
    g.set_defaults(fn=cmd_gen)
    e = sub.add_parser("emit", help="re-emit sources from a case.json")
    e.add_argument("case")
    e.add_argument("--out", required=True)
    e.set_defaults(fn=cmd_emit)
    c = sub.add_parser("check", help="check case.json files")
    c.add_argument("cases", nargs="+")
    c.set_defaults(fn=cmd_check)
    runcli.register(sub)   # run, campaign, buckets (and the internal worker)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
