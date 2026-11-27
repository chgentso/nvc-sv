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


"""Run configurations and tool drivers.

One Config describes one way of running a case (a nvc island mode, GHDL).  A
RunResult is what a config produced: outcome class (level L0), merged text
output and the waveform file.  Nothing here compares results.

Resource rules (plan section 7): every tool call has a hard timeout and runs
in its own process group (killed by pid, never by name); core dumps off;
the JIT cache is private per nvc binary and per worker, because the shared
cache is keyed on the git SHA only.
"""

import hashlib
import os
import re
import resource
import shutil
import signal
import subprocess
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

TOOL_TIMEOUT = 60          # seconds, per tool call (hard ceiling 300)
STOP_DELTA = 1000

# same pattern as the external-suite harness (crash = signal, internal error,
# sanitizer text)
CRASH_RE = re.compile(
    r'internal error|Caught signal|\*\*\* Caught|AddressSanitizer|SIGSEGV|'
    r'SEGV on|nvc[^\n]*Assertion .* failed|LeakSanitizer|'
    r'UndefinedBehaviorSanitizer|runtime error:|Stack dump|abort\(\)|'
    r'Aborted|core dumped|fatal_trace', re.I)
UNSUPPORTED_RE = re.compile(r'\bsorry\b|not (?:yet )?(?:supported|implemented)',
                            re.I)
DELTA_LIMIT_RE = re.compile(r'iteration limit|limit of \d+ delta cycles|'
                            r'delta cycle limit|too many delta', re.I)
ERROR_RE = re.compile(r'^\*\* (?:Error|Fatal): ', re.M)
FAILURE_RE = re.compile(r'^\*\* (?:Failure|Fatal): ', re.M)

OUTCOMES = ("ok", "failure", "reject", "unsupported", "crash", "timeout",
            "deltalimit", "incomplete")


def finished(case, outcome, text):
    """A clean exit counts as ok only if the end marker report is there."""
    if outcome == "ok" and case.end_marker and case.end_marker not in text:
        return "incomplete"
    return outcome


@dataclass(frozen=True)
class Config:
    name: str
    tool: str = "nvc"                     # "nvc" | "ghdl"
    elab_args: Tuple[str, ...] = ()
    elab_env: Tuple[Tuple[str, str], ...] = ()
    run_args: Tuple[str, ...] = ()
    run_env: Tuple[Tuple[str, str], ...] = ()
    waves: bool = True
    needs: Tuple[str, ...] = ()           # capabilities the build must have

    def elab_key(self):
        return (self.tool, self.elab_args, self.elab_env)


# plan section 5.2 (C2 subset); C7 adds the rest
_SPEC = (("NVC_ISLAND_SPEC", "1"),)
CONFIGS: Dict[str, Config] = {c.name: c for c in (
    Config("ref", elab_args=("--rtl-islands=off",), needs=("rtl_islands",)),
    Config("default"),
    Config("nofast", run_args=("--no-island-fast",),
           needs=("no_island_fast",)),
    Config("spec", elab_env=_SPEC),
    Config("specA", elab_env=_SPEC,
           run_env=(("NVC_ISLAND_SPEC_FORCE", "A"),)),
    Config("ghdl", tool="ghdl"),
)}
DEFAULT_CONFIGS = ("ref", "default", "nofast", "spec", "specA", "ghdl")


@dataclass
class Build:
    """An nvc build directory (bin/nvc-sv, lib/)."""
    path: str
    label: str = "nvc"
    bin: str = ""
    lib: str = ""
    version: str = ""
    caps: Dict[str, bool] = field(default_factory=dict)

    def __post_init__(self):
        self.path = os.path.abspath(self.path)
        self.bin = os.path.join(self.path, "bin", "nvc-sv")
        self.lib = os.path.join(self.path, "lib")
        if not os.path.exists(self.bin):
            raise FileNotFoundError(self.bin)


@dataclass
class Tools:
    """Everything a run needs from the outside world."""
    build: Build
    ghdl: Optional[str] = None            # path of the ghdl binary or None
    cache_root: str = ""                  # worker-private scratch (JIT cache, tmp)
    timeout: int = TOOL_TIMEOUT


@dataclass
class RunResult:
    config: str
    outcome: str = "ok"
    stage: str = "run"                    # last stage reached
    rc: int = 0
    text: str = ""                        # merged output of the failing or run stage
    wave: Optional[str] = None            # VCD path, if produced
    wall: float = 0.0
    elab_text: str = ""
    available: bool = True                # False: tool could not handle the case


def worker_env(tools: Tools, extra=None):
    """Environment of one tool call: private JIT cache, cache and tmp dirs."""
    e = dict(os.environ)
    root = tools.cache_root
    if root:
        os.makedirs(root, exist_ok=True)
        st = os.stat(tools.build.bin)
        jit = os.path.join(root, "jit",
                           "%s-%d-%d" % (tools.build.label, st.st_size,
                                         st.st_mtime_ns))
        tmp = os.path.join(root, "tmp")
        xdg = os.path.join(root, "xdg")
        for d in (jit, tmp, xdg):
            os.makedirs(d, exist_ok=True)
        e["NVC_JIT_CACHE_DIR"] = jit
        e["TMPDIR"] = tmp
        e["XDG_CACHE_HOME"] = xdg
    if extra:
        e.update(extra)
    return e


def _no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def call(cmd, cwd, env, timeout):
    """Run cmd in its own process group; stderr merged into stdout.
    Returns (rc, text, timed_out, wall).  A timeout kills only that group."""
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         start_new_session=True, preexec_fn=_no_core)
    try:
        out, _ = p.communicate(timeout=timeout)
        timed_out = False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        out, _ = p.communicate()
        timed_out = True
    text = out.decode("utf-8", errors="replace") if out else ""
    return (-9 if timed_out else p.returncode), text, timed_out, \
        time.monotonic() - t0


def classify(rc, text, timed_out):
    """Outcome class (level L0) of one finished stage."""
    if timed_out:
        return "timeout"
    if rc < 0 or CRASH_RE.search(text):
        return "crash"
    if DELTA_LIMIT_RE.search(text) and rc != 0:
        return "deltalimit"
    if UNSUPPORTED_RE.search(text) and rc != 0:
        return "unsupported"
    if rc == 0:
        return "ok"
    return "failure"


def probe_build(build: Build, tools_env=None):
    """Capability probe from `nvc --help` (cached on the Build)."""
    if build.caps:
        return build.caps
    rc, text, _, _ = call([build.bin, "--help"], os.getcwd(),
                          tools_env or dict(os.environ), 30)
    build.caps = {"report_order": "--report-order" in text,
                  "rtl_islands": "--rtl-islands" in text,
                  "no_island_fast": "--no-island-fast" in text}
    _, vtext, _, _ = call([build.bin, "--version"], os.getcwd(),
                          tools_env or dict(os.environ), 30)
    build.version = (vtext.splitlines() or ["?"])[0]
    return build.caps


# ------------------------------------------------------------------ nvc

def _std_arg(case):
    return "--std=" + case.std


def _nvc_base(tools, case, workdir):
    # the library 'work' lives in the cwd of each call (the elab dir)
    return [tools.build.bin, "-L" + tools.build.lib, _std_arg(case),
            "--work=work"]


def run_nvc(tools: Tools, case, cfg: Config, workdir, elab_done: dict,
            waves=None) -> RunResult:
    """Analyse + elaborate (once per elab key) and run.  The workdir of an
    elab key is shared by the configs that only differ at run time."""
    res = RunResult(cfg.name)
    waves = cfg.waves and case.waves if waves is None else waves
    key = cfg.elab_key()
    base_env = worker_env(tools)
    if key not in elab_done:
        edir = os.path.join(workdir, "e%d" % len(elab_done))
        os.makedirs(edir, exist_ok=True)
        env = dict(base_env)
        env.update(dict(cfg.elab_env))
        t = 0.0
        rc, text, to, w = call(_nvc_base(tools, case, edir) + ["-a"] +
                               case.files, edir, env, tools.timeout)
        t += w
        stage = "analyse"
        if rc == 0 and not to:
            rc, text, to, w = call(_nvc_base(tools, case, edir) + ["-e"] +
                                   list(cfg.elab_args) + [case.top], edir,
                                   env, tools.timeout)
            t += w
            stage = "elab"
        elab_done[key] = (edir, rc, text, to, stage)
    edir, rc, text, to, stage = elab_done[key]
    if rc != 0 or to:
        res.stage, res.rc, res.text = stage, rc, text
        res.outcome = _stage_fail(rc, text, to)
        return res
    res.elab_text = text
    wave = os.path.join(workdir, "wave_%s.vcd" % cfg.name)
    cmd = _nvc_base(tools, case, edir) + ["-r"] + list(cfg.run_args)
    cmd += ["--stop-time=" + case.stop_time, "--stop-delta=%d" % STOP_DELTA]
    if waves:
        cmd += ["--wave=" + wave, "--format=vcd", "--dump-arrays"]
    cmd += [case.top]
    env = dict(base_env)
    env.update(dict(cfg.run_env))
    rc, text, to, w = call(cmd, edir, env, tools.timeout)
    res.stage, res.rc, res.text, res.wall = "run", rc, text, w
    res.outcome = finished(case, classify(rc, text, to), text)
    if waves and os.path.exists(wave):
        res.wave = wave
    return res


def _stage_fail(rc, text, to):
    """A failed analysis/elaboration: reject unless it is a crash, timeout or
    a 'sorry' (unsupported)."""
    o = classify(rc, text, to)
    return "reject" if o in ("failure", "deltalimit") else o


# ----------------------------------------------------------------- GHDL

def ghdl_std(case):
    return {"1993": "93", "2002": "02", "2008": "08", "2019": "19"}.get(
        case.std, case.std)


def run_ghdl(tools: Tools, case, cfg: Config, workdir, waves=None) -> RunResult:
    res = RunResult(cfg.name)
    if case.lang != "vhdl" or not tools.ghdl:
        res.available = False
        res.outcome = "unsupported"
        return res
    waves = cfg.waves and case.waves if waves is None else waves
    gd = os.path.join(workdir, "ghdl")
    os.makedirs(gd, exist_ok=True)
    env = worker_env(tools)
    std = "--std=" + ghdl_std(case)
    g = tools.ghdl
    rc, text, to, w = call([g, "-a", std, "--workdir=" + gd] + case.files,
                           gd, env, tools.timeout)
    stage = "analyse"
    if rc == 0 and not to:
        rc, text, to, w2 = call([g, "-e", std, "--workdir=" + gd, case.top],
                                gd, env, tools.timeout)
        w += w2
        stage = "elab"
    if rc != 0 or to:
        # an analysis or elaboration problem drops the tool for this case
        res.stage, res.rc, res.text, res.available = stage, rc, text, False
        res.outcome = _stage_fail(rc, text, to)
        return res
    wave = os.path.join(gd, "wave_ghdl.vcd")
    cmd = [g, "-r", std, "--workdir=" + gd, case.top,
           "--stop-time=" + case.stop_time, "--stop-delta=%d" % STOP_DELTA,
           "--ieee-asserts=disable"]
    if waves:
        cmd.append("--vcd=" + wave)
    rc, text, to, w = call(cmd, gd, env, tools.timeout)
    res.stage, res.rc, res.text, res.wall = "run", rc, text, w
    res.outcome = finished(case, classify(rc, text, to), text)
    if waves and os.path.exists(wave):
        res.wave = wave
    return res


# ------------------------------------------------------------- one case

def run_case(tools: Tools, case, config_names=DEFAULT_CONFIGS, workdir=None,
             configs=None) -> Dict[str, RunResult]:
    """Run all configs of a case; returns name -> RunResult."""
    configs = configs or CONFIGS
    workdir = workdir or os.path.join(tools.cache_root or ".", "case-" +
                                      case.name)
    os.makedirs(workdir, exist_ok=True)
    elab_done: dict = {}
    out = {}
    for name in config_names:
        cfg = configs[name]
        missing = [c for c in cfg.needs
                   if not probe_build(tools.build, worker_env(tools)).get(c)] \
            if cfg.tool == "nvc" else []
        if missing:
            # an older build without the option: the config is not run
            out[name] = RunResult(name, outcome="unsupported",
                                  available=False,
                                  text="build lacks " + ",".join(missing))
        elif cfg.tool == "ghdl":
            out[name] = run_ghdl(tools, case, cfg, workdir)
        else:
            out[name] = run_nvc(tools, case, cfg, workdir, elab_done)
    return out


def island_stats(tools: Tools, case, workdir):
    """Fused-process share from NVC_DUMP_ISLANDS=1 at elaboration (not
    compared; statistics only)."""
    d = os.path.join(workdir, "stats")
    os.makedirs(d, exist_ok=True)
    env = worker_env(tools, {"NVC_DUMP_ISLANDS": "1"})
    base = _nvc_base(tools, case, d)
    rc, text, to, _ = call(base + ["-a"] + case.files, d, env, tools.timeout)
    if rc == 0 and not to:
        rc, text, to, _ = call(base + ["-e", case.top], d, env, tools.timeout)
    return parse_island_stats(text)


def parse_island_stats(text):
    st = dict(fused_procs=0, total_procs=0, islands=0)
    for line in text.splitlines():
        m = re.search(r'VHDL_ISLAND_ELAB: .*?islands=(\d+) '
                      r'fused_procs=(\d+)/(\d+)', line)
        if m:
            st["islands"] += int(m.group(1))
            st["fused_procs"] += int(m.group(2))
            st["total_procs"] += int(m.group(3))
    return st
