# Constrained random solver benchmarks

One SystemVerilog file per case: a class with one `randomize()` in a loop of `+N=<calls>`.
`run.sh <build dir> [--quick] [--slack F] [name ...]` times them (per-call time is the difference between
a run of N calls and a run of one, over N - 1) and exits 1 if a case that should pass is over its limit,
gives up in a call, or does not run.

* `limits.txt`: calls, limit in microseconds per call, expectation and quick-set flag for every case.
  A case that passes in release 0.18.0 is limited to 3x the slower of the baseline and the figure of
  the plan; the others to the target of the plan (section 7) and marked `xfail:<milestone>` until that
  milestone is meant to reach it. `run.sh` prints XPASS when one of them passes: remove its mark.
* `baseline.tsv`: the per-call time and give-ups of release 0.18.0 (name, calls, us per call, result
  line), measured on a machine that was also building; read to one significant digit.
* `baseline-m2.tsv`: solver time per call of the legacy engine (the search of release 0.18.0) and of `auto`
  in the same build, read from `NVC_CRV_STATS`; the end-to-end figures of
  `baseline.tsv` also hold the interpreter running the generated code and, for a big constraint,
  its compilation.
* `baseline-m3.tsv`: the same figures for `auto` after the rejection stage (crv M3), next to those of M2.
* `bias-reject.txt`: the chi-square report of the forced rejection stage (it passes every problem, three
  seeds) and of `auto` (it fails the problems whose acceptance is too low for 16 samples, which fall to the
  search; the BDD stage is for those).
* `bias-legacy.txt`: the chi-square report of the search of release 0.18.0 against the distribution the
  language asks for, and the result of the differential fuzzer (`NVC_CRV_LONG=1 bin/unit_test crv`).

`--quick` runs the five fastest cases (a few seconds) and is part of the light gate.
