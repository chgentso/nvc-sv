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


"""Campaign driver: cases in, cases.jsonl and buckets out (plan section 7).

Resources: default is 2 workers, each one long process under one 3G scope
(slot script from HDLFUZZ_SLOT_SH when set, else systemd-run with the same
cap), so a campaign never exceeds the lane's slots.  Every tool call has its
own hard timeout (run.call).  A campaign stops below 12 GB free disk, when
its folder passes 2 GB, or when a file named STOP appears in it.

Until the owner allows campaigns, more than SMOKE_LIMIT cases are refused
unless HDLFUZZ_ALLOW_CAMPAIGN=1 is set.
"""

import json
import os
import shutil
import subprocess
import sys
import time

from . import caseio, compare, run

SMOKE_LIMIT = 20
MIN_FREE = 12 * 1024 ** 3
MAX_OUT = 2 * 1024 ** 3
SLOT_CAP = "3G"


def slug(sig):
    return "".join(c if c.isalnum() or c in "-_." else "_"
                   for c in sig.replace("/", "~"))[:120]


def versions(tools, out):
    info = {"nvc": tools.build.version, "nvc_build": os.path.basename(
        tools.build.path), "python": sys.version.split()[0]}
    if tools.ghdl:
        _, text, _, _ = run.call([tools.ghdl, "--version"], out,
                                 dict(os.environ), 30)
        info["ghdl"] = (text.splitlines() or ["?"])[0]
    try:
        info["fuzzer"] = subprocess.run(
            ["git", "-C", os.path.dirname(os.path.abspath(__file__)),
             "rev-parse", "--short", "HEAD"], capture_output=True,
            text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        pass
    return info


def append_line(path, rec):
    """One write per record: lines of several workers do not interleave."""
    data = (json.dumps(rec, sort_keys=True) + "\n").encode()
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
    try:
        os.write(fd, data)
    finally:
        os.close(fd)


def dir_size(path):
    n = 0
    for root, _, files in os.walk(path):
        for f in files:
            try:
                n += os.path.getsize(os.path.join(root, f))
            except OSError:
                pass
    return n


def stop_reason(out):
    if os.path.exists(os.path.join(out, "STOP")):
        return "STOP file"
    if shutil.disk_usage(out).free < MIN_FREE:
        return "less than 12 GB free"
    if dir_size(out) > MAX_OUT:
        return "campaign folder over 2 GB"
    return None


def run_one(tools, case, config_names, out, worker_dir, tier="", seed=None,
            confirm=True, stats=True, report_stable=False):
    """Run and compare one case; returns the jsonl record.  A differing case
    is run a second time and keeps only the signatures seen twice."""
    t0 = time.time()
    wd = os.path.join(worker_dir, "case-" + case.name)
    results = run.run_case(tools, case, config_names, wd)
    verdict = compare.compare_case(results, report_stable=report_stable)
    sigs = verdict.signatures(group=True)
    reproduced = None
    if sigs and confirm:
        wd2 = wd + "-again"
        res2 = run.run_case(tools, case, config_names, wd2)
        v2 = compare.compare_case(res2, report_stable=report_stable)
        again = set(v2.signatures(group=True))
        reproduced = bool(set(sigs) & again)
        verdict.notes.append("reproduced" if reproduced
                             else "not reproduced: " + ",".join(sorted(again)))
        sigs = [s for s in sigs if s in again] or sigs
        shutil.rmtree(wd2, ignore_errors=True)
    rec = {
        "case": case.name, "seed": seed, "tier": tier, "lang": case.lang,
        "outcomes": {k: r.outcome for k, r in results.items()},
        "walls": {k: round(r.wall, 3) for k, r in results.items()},
        "signatures": sigs, "configs": verdict.configs(),
        "primary": verdict.primary(True) if sigs else "",
        "blame": verdict.blame, "notes": verdict.notes,
        "info": verdict.info, "reproduced": reproduced,
        "size": case.source_size(), "time": round(time.time() - t0, 3),
    }
    if stats and case.lang == "vhdl":
        rec["islands"] = run.island_stats(tools, case, wd)
    if sigs:
        keep(out, case, results, verdict, rec)
    shutil.rmtree(wd, ignore_errors=True)
    return rec


def keep(out, case, results, verdict, rec):
    """Failing case: sources, logs, waveforms and the verdict stay."""
    d = os.path.join(out, "fail", slug(rec["primary"] or "unknown"), case.name)
    os.makedirs(d, exist_ok=True)
    for f in case.files:
        shutil.copy(f, d)
    cj = os.path.join(case.dir, "case.json")
    if os.path.exists(cj):
        shutil.copy(cj, d)
    for name, r in results.items():
        with open(os.path.join(d, name + ".log"), "w") as f:
            f.write(r.text)
        if r.wave and os.path.exists(r.wave):
            shutil.copy(r.wave, os.path.join(d, name + ".vcd"))
    with open(os.path.join(d, "verdict.json"), "w") as f:
        json.dump({"signatures": verdict.signatures(),
                   "grouped": verdict.signatures(True),
                   "blame": verdict.blame,
                   "notes": verdict.notes, "info": verdict.info,
                   "diffs": [vars(x) for x in verdict.diffs]}, f, indent=1,
                  sort_keys=True)


def build_buckets(jsonl, keep_n=3):
    """grouped signature -> {count, cases: the keep_n smallest cases,
    configs: {config: number of cases it showed the difference in}}."""
    b = {}
    if not os.path.exists(jsonl):
        return b
    with open(jsonl) as f:
        for line in f:
            try:
                r = json.loads(line)
            except ValueError:
                continue
            for s in r.get("signatures", []):
                e = b.setdefault(s, {"count": 0, "cases": [], "configs": {}})
                e["count"] += 1
                for c in r.get("configs", {}).get(s, []):
                    e["configs"][c] = e["configs"].get(c, 0) + 1
                e["cases"].append((r.get("size", 0), r["case"]))
                e["cases"] = sorted(e["cases"])[:keep_n]
    return {s: {"count": e["count"], "cases": [c for _, c in e["cases"]],
                "configs": dict(sorted(e["configs"].items()))}
            for s, e in sorted(b.items())}


def write_buckets(out):
    b = build_buckets(os.path.join(out, "cases.jsonl"))
    with open(os.path.join(out, "buckets.json"), "w") as f:
        json.dump(b, f, indent=1, sort_keys=True)
    return b


def worker_loop(tools, items, config_names, out, worker_dir, tier="",
                **kw):
    """items: iterable of (name-or-seed, Case).  Appends to cases.jsonl."""
    done = 0
    for seed, case in items:
        why = stop_reason(out)
        if why:
            print("worker stops: " + why, file=sys.stderr)
            break
        rec = run_one(tools, case, config_names, out, worker_dir, tier,
                      seed, **kw)
        append_line(os.path.join(out, "cases.jsonl"), rec)
        done += 1
    return done


def slot_prefix(cap=SLOT_CAP):
    """Command prefix that puts a worker under one memory-capped scope."""
    slot = os.environ.get("HDLFUZZ_SLOT_SH")
    if slot:
        return [slot, cap]
    return ["systemd-run", "--user", "--scope", "-q", "-p",
            "MemoryMax=" + cap, "-p", "MemorySwapMax=0"]


def worker_commands(args_for_worker, workers=2, cap=SLOT_CAP):
    """One command per worker; args_for_worker(i, n) -> argument list of the
    worker subcommand."""
    pkg = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cmds = []
    for i in range(workers):
        cmds.append((slot_prefix(cap) + [sys.executable, "-m",
                                         "hdlfuzz.runcli", "worker"] +
                     args_for_worker(i, workers), pkg))
    return cmds


def launch(cmds, out):
    """Start the workers, wait for them; returns their exit codes."""
    procs = []
    for i, (cmd, pkg) in enumerate(cmds):
        env = dict(os.environ)
        env["PYTHONPATH"] = pkg + os.pathsep + env.get("PYTHONPATH", "")
        log = open(os.path.join(out, "worker%d.log" % i), "w")
        procs.append(subprocess.Popen(cmd, env=env, stdout=log, stderr=log,
                                      stdin=subprocess.DEVNULL))
    return [p.wait() for p in procs]
