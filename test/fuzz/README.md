# hdlfuzz: differential fuzzer for nvc

`hdlfuzz` generates random but legal VHDL designs with their own testbench,
simulates each one in several nvc configurations (and optionally GHDL), and
buckets every difference by a stable signature. Its main purpose is to keep the
RTL island optimisations exact: every island mode must give the same reports
and waveforms as `--rtl-islands=off`.

Python 3, standard library only. Nothing here is built by `make`; run it from
this directory (`test/fuzz`).

```
python3 -m hdlfuzz gen      --cases 0..9 --out /tmp/cases      # generate case directories
python3 -m hdlfuzz check    /tmp/cases/c*/case.json            # legality check of the IR
python3 -m hdlfuzz emit     case.json --out dir                # re-print sources from the IR
python3 -m hdlfuzz run      --build <nvc build dir> --work /tmp/w CASEDIR...
python3 -m hdlfuzz campaign --build <nvc build dir> --seeds 0..4999 --tier V1 \
                            --workers 2 --out /tmp/camp
python3 -m hdlfuzz buckets  /tmp/camp                          # bucket summary
python3 -m unittest discover -s tests -t .                     # unit tests (no simulator needed)
```

## Cases

A case is a directory with `case.json` (`format`, `run`, `ir`), `dut.vhd` and
`tb.vhd`; the top entity is `fz_tb`, the design instance `dut`. See
[FORMAT.md](FORMAT.md). The same seed always gives the same bytes. Hand-written
cases in the same layout live in `cases/`.

Tier V1 (`hdlfuzz/gen.py`): concurrent and selected assignments, combinational
processes (`process (all)` or full lists), flip-flop processes (both edges,
sync reset either polarity or none), `std_logic`, `unsigned`, `signed`,
`std_logic_vector` of 1 to 130 bits, `numeric_std` arithmetic, shifts,
reductions, slices, concatenation, hierarchy up to three levels, seven
profiles. Inputs change at the falling edge, in the clock-edge delta or one to
three deltas after it, so delta-timing bugs are exercised. Observers: per-signal
monitors (`FZ M`), a per-cycle sampler (`FZ S`), a final dump (`FZ F`), the end
marker (`FZ END`) and an optional VCD.

## Configurations (`--configs`, default all)

| name | what |
|---|---|
| `ref` | `--rtl-islands=off` (the reference) |
| `default` | islands with the default options |
| `nofast` | `--no-island-fast` |
| `spec` | elaborated with `NVC_ISLAND_SPEC=1` (speculative 2-state islands), run with `NVC_ISLAND_SPEC_START=0` (promotion from the start) |
| `specA` | `spec` plus `NVC_ISLAND_SPEC_FORCE=A` (all eligible islands in 2-state from t=0) |
| `ghdl` | GHDL, when `--ghdl` points at a binary (`auto` searches `PATH`) |

A configuration whose option the build does not know is skipped with a note.
`--nvc-run-arg` adds arguments to every nvc run step (for triage, e.g.
`--ieee-warnings=off`); `--report-stable` tells the comparison that the build
orders reports itself (`--report-order=stable`, the default in nvc-sv), so
reports inside one delta are not sorted before comparing.

## Comparison levels

- L0: outcome class (ok, failure, crash, timeout, unsupported, reject,
  deltalimit, incomplete).
- L1: reports parsed to (severity, time, delta, text), and the VCD as a
  canonical hash with a first-difference search; IEEE package warnings are a
  separate comparison (`l1-ieee/...`).
- L2: nvc reports against GHDL as a multiset per time, without deltas.
- L3: nvc VCD against GHDL VCD on common names, nine values reduced to four,
  last value per time.

Every normalisation is a named rule recorded with the difference.

## Signatures and buckets

`<level>-<what>/<cfg>-vs-<ref>/<kind>/<shape>`, lower case, with no numbers,
names or paths (monitor values are masked), so one bug gives one bucket. The
island configurations (`default`, `nofast`, `spec`, `specA`) share one bucket
(`islands-vs-ref`) and keep per-configuration counts. A differing case is rerun
once and kept only if the difference reproduces; a bucket keeps its five
smallest cases on disk. Results: `<out>/cases.jsonl`, `<out>/buckets.json`,
`<out>/fail/<bucket>/<case>`.

## Campaigns and resources

- A campaign refuses more than 20 cases unless `HDLFUZZ_ALLOW_CAMPAIGN=1` is set.
- `HDLFUZZ_SLOT_SH=<script>` runs every worker through a slot script (a lock
  that limits concurrent heavy jobs and applies a memory cap); without it,
  workers use `systemd-run --user --scope -p MemoryMax=<cap>`.
- Every tool call has a timeout (`--timeout`, default 60 s) in its own process
  group, with core dumps off; JIT cache, `TMPDIR` and `XDG_CACHE_HOME` are
  private per worker and per binary.
- Throughput: about 6,700 cases per hour with two workers (six configurations
  each, cases of under a second).
- `--dry-run` prints the worker commands.

## Triage

1. `python3 -m hdlfuzz buckets <out>` lists buckets with counts and classes.
2. Rerun one case: `python3 -m hdlfuzz run --build <build> --work /tmp/t <out>/fail/<bucket>/<case>`.
3. Class A: a real bug (islands differ from `ref`, or nvc differs from GHDL
   where the LRM is clear). Class B: an order the LRM leaves open (needs a
   named normalisation rule). Class C: a generator or harness bug.
4. Reduce the case by hand (the reducer is planned) and add it as a regression
   test under `test/regress`; name the test after the root cause.

## Recall check

A build from before the VHDL island delta-timing fix (`c22a0917`) shows the
comb-chain delta collapse in 16 of the first 500 V1 seeds (signature
`l1-report/.../delta-shift/note:fz m #`, first at seed 181). Rerunning this on a
pre-fix build is the quick check that the fuzzer still finds that class.

## Status and plan

First campaign (5,000 V1 seeds): zero differences against GHDL; found the
island slice-wake, a GVN crash, a missing evaluation, extra metavalue warnings
from islands, report-order differences, and two speculative 2-state value bugs
(`resize(signed, 1)`, a wide signed multiply). After those fixes, 1,000 seeds
are exact in `default` and `nofast`; the remaining differences are in `spec` /
`specA` only. Planned next: the reducer (C4/C5), tier V2 (RAMs, generate,
functions, records, resets) and an SV tier, more configurations (threads, JIT
vs interpreter, `-O0`), an observer plugin for VPI/VHPI, and a fixed-seed smoke
run for the light gate.
