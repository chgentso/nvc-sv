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


"""Command line of the runner lane:

  python3 -m hdlfuzz.runcli run CASE... --build DIR [--configs a,b] [--ghdl P]
  python3 -m hdlfuzz.runcli campaign --build DIR --out DIR (--cases DIR...|
                            --seeds A..B --tier V1) [--workers 2] [--dry-run]
  python3 -m hdlfuzz.runcli buckets OUT

`run` is a development tool (one job, no scope): start it under the lane's
slot script.  `campaign` starts its workers under one capped scope each;
without the owner's go (HDLFUZZ_ALLOW_CAMPAIGN=1) it refuses more than 20
cases.  `worker` is internal.
"""

import argparse
import json
import os
import shutil
import sys

from . import campaign, caseio, compare, run


def find_cases(paths):
    """A path is a case dir (has sources) or a folder of case dirs."""
    out = []
    for p in paths:
        p = os.path.abspath(p)
        if any(n.endswith(caseio.VHDL_EXT + caseio.SV_EXT)
               for n in os.listdir(p)):
            out.append(p)
        else:
            out += [os.path.join(p, n) for n in sorted(os.listdir(p))
                    if os.path.isdir(os.path.join(p, n))]
    return out


def generate_case(seed, tier, dest):
    """Hook for the generator (chunk C3 wires hdlfuzz.gen/testbench here):
    must write the case directory under dest and return its path."""
    raise SystemExit("seed mode: the generator is not wired to the runner "
                     "yet (use --cases)")


def parse_seeds(s):
    a, _, b = s.partition("..")
    return range(int(a), int(b or a) + 1)


def make_tools(args, cache_root):
    b = run.Build(args.build, args.label)
    tools = run.Tools(b, cache_root=cache_root, timeout=args.timeout)
    g = args.ghdl
    if g == "auto":
        g = shutil.which("ghdl")
    tools.ghdl = g if g and g != "none" else None
    run.probe_build(b, run.worker_env(tools))
    return tools


def add_common(ap):
    ap.add_argument("--build", required=True, help="nvc build directory")
    ap.add_argument("--label", default="nvc")
    ap.add_argument("--ghdl", default="auto", help="ghdl binary, auto or none")
    ap.add_argument("--configs", default=",".join(run.DEFAULT_CONFIGS))
    ap.add_argument("--timeout", type=int, default=run.TOOL_TIMEOUT)
    ap.add_argument("--report-stable", action="store_true",
                    help="build orders reports itself: no N1 sort")


def cmd_run(args):
    work = os.path.abspath(args.work)
    tools = make_tools(args, os.path.join(work, "cache"))
    names = args.configs.split(",")
    status = 0
    for d in find_cases(args.cases):
        case = caseio.load_case(d)
        results = run.run_case(tools, case, names,
                               os.path.join(work, "case-" + case.name))
        ld = os.path.join(work, "logs", case.name)
        os.makedirs(ld, exist_ok=True)
        for k, r in results.items():
            with open(os.path.join(ld, k + ".log"), "w") as f:
                f.write("outcome=%s stage=%s rc=%d\n%s" % (
                    r.outcome, r.stage, r.rc, r.text))
        v = compare.compare_case(results, report_stable=args.report_stable)
        print("%s: %s" % (case.name, " ".join(
            "%s=%s" % (k, r.outcome) for k, r in results.items())))
        for n in v.notes:
            print("  note: " + n)
        for df in v.diffs:
            print("  %s\n      %s [%s]" % (df.signature(), df.detail,
                                           ",".join(df.rules)))
            status = 1
        if v.blame:
            print("  blame: " + v.blame)
    return status


def cmd_campaign(args):
    out = os.path.abspath(args.out)
    if args.cases:
        ncases = len(find_cases(args.cases))
    else:
        ncases = len(parse_seeds(args.seeds))
    if ncases > campaign.SMOKE_LIMIT and \
       os.environ.get("HDLFUZZ_ALLOW_CAMPAIGN") != "1":
        raise SystemExit("%d cases: campaigns need the owner's go "
                         "(HDLFUZZ_ALLOW_CAMPAIGN=1); limit is %d" %
                         (ncases, campaign.SMOKE_LIMIT))

    def wargs(i, n):
        a = ["--build", args.build, "--label", args.label, "--ghdl",
             args.ghdl, "--configs", args.configs, "--out", out,
             "--timeout", str(args.timeout), "--shard", "%d/%d" % (i, n),
             "--tier", args.tier]
        if args.report_stable:
            a.append("--report-stable")
        a += (["--cases"] + args.cases) if args.cases else \
            ["--seeds", args.seeds]
        return a

    cmds = campaign.worker_commands(wargs, args.workers, args.cap)
    if args.dry_run:
        for c, _ in cmds:
            print(" ".join(c))
        return 0
    os.makedirs(out, exist_ok=True)
    tools = make_tools(args, os.path.join(out, "work", "probe"))
    with open(os.path.join(out, "versions.json"), "w") as f:
        json.dump(campaign.versions(tools, out), f, indent=1, sort_keys=True)
    codes = campaign.launch(cmds, out)
    b = campaign.write_buckets(out)
    print("workers exit %s; %d buckets" % (codes, len(b)))
    return 1 if any(codes) else 0


def cmd_worker(args):
    out = os.path.abspath(args.out)
    i, n = map(int, args.shard.split("/"))
    wdir = os.path.join(out, "work", "w%d" % i)
    tools = make_tools(args, wdir)
    names = args.configs.split(",")
    if args.cases:
        paths = find_cases(args.cases)[i::n]
        items = ((os.path.basename(p), caseio.load_case(p)) for p in paths)
    else:
        seeds = list(parse_seeds(args.seeds))[i::n]

        def gen():
            for s in seeds:
                d = generate_case(s, args.tier,
                                  os.path.join(wdir, "gen", "s%d" % s))
                yield s, caseio.load_case(d)
        items = gen()
    done = campaign.worker_loop(tools, items, names, out, wdir, args.tier,
                                report_stable=args.report_stable)
    print("worker %d: %d cases" % (i, done))
    return 0


def cmd_buckets(args):
    b = campaign.write_buckets(os.path.abspath(args.out))
    for s, e in b.items():
        print("%5d  %s  %s" % (e["count"], s, ",".join(e["cases"])))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="hdlfuzz.runcli")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("run")
    add_common(p)
    p.add_argument("cases", nargs="+")
    p.add_argument("--work", required=True)
    p.set_defaults(fn=cmd_run)

    for name, fn in (("campaign", cmd_campaign), ("worker", cmd_worker)):
        p = sub.add_parser(name)
        add_common(p)
        p.add_argument("--out", required=True)
        p.add_argument("--tier", default="V1")
        g = p.add_mutually_exclusive_group(required=True)
        g.add_argument("--cases", nargs="+")
        g.add_argument("--seeds")
        if name == "campaign":
            p.add_argument("--workers", type=int, default=2)
            p.add_argument("--cap", default=campaign.SLOT_CAP)
            p.add_argument("--dry-run", action="store_true")
        else:
            p.add_argument("--shard", required=True)
        p.set_defaults(fn=fn)

    p = sub.add_parser("buckets")
    p.add_argument("out")
    p.set_defaults(fn=cmd_buckets)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
