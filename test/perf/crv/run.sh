#!/bin/bash
#
# Performance guard for the constrained random solver.
#
#   run.sh <build dir> [--quick] [--slack F] [name ...]
#
# Each case is a SystemVerilog file in this directory with one randomize()
# in a loop of +N=<calls>.  The time per call is the difference between a
# run of N calls and a run of one call divided by N - 1, which removes the
# start-up and the compilation of the design.  limits.txt gives for each
# case the number of calls, the limit in microseconds per call, the
# expected result (pass, or xfail:<milestone> for a case that is known to be
# over its limit or to fail until that milestone) and whether it belongs to
# the quick set.  The limit of a case that passes today is three times
# today's time or more; the limit of the others is the target.
#
# Exit status is 1 if a case that should pass is over limit * slack, gives up
# in a call or fails to run.  A case marked xfail that does pass is reported
# as XPASS: remove its mark.
#
# The time per call is the time the solver itself spends (graph to solution,
# counted by NVC_CRV_STATS) for a case of fewer than 100000 calls and the
# difference of two runs for the others, which are too fast for the
# statistics to be left on.  NVC_CRV_ENGINE and the other switches in the
# environment apply to the runs.
#
# Memory is capped with systemd-run when it is there, every run takes at most
# 300 seconds.

ulimit -c 0

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=
SOLVER_BELOW=100000
QUICK=0
SLACK=1
NAMES=()

while [ $# -gt 0 ]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --slack) SLACK=$2; shift ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) if [ -z "$BUILD" ]; then BUILD=$1; else NAMES+=("$1"); fi ;;
    esac
    shift
done

[ -n "$BUILD" ] || { echo "usage: run.sh <build dir> [--quick] [--slack F] [name ...]" >&2; exit 2; }
BUILD=$(cd "$BUILD" && pwd)
NVC=("$BUILD/bin/nvc" -L "$BUILD/lib" --std=2019)

CAP=()
if command -v systemd-run >/dev/null 2>&1 \
   && systemd-run --user --scope -q true >/dev/null 2>&1; then
    CAP=(systemd-run --user --scope -q -p MemoryMax=6G -p MemorySwapMax=0)
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/crvperf.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

now() { date +%s.%N; }

bad=0
printf '%-16s %8s %12s %12s  %s\n' case calls us/call limit result

while read -r name calls limit expect quick _; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ ${#NAMES[@]} -gt 0 ]; then
        match=0
        for n in "${NAMES[@]}"; do [ "$n" = "$name" ] && match=1; done
        [ $match = 1 ] || continue
    elif [ $QUICK = 1 ] && [ "$quick" != 1 ]; then
        continue
    fi

    d=$WORK/$name
    mkdir -p "$d"
    cd "$d" || exit 2
    if ! "${CAP[@]}" timeout 300 "${NVC[@]}" -a "$HERE/$name.sv" -e top \
         > build.log 2>&1; then
        printf '%-16s %8s %12s %12s  %s\n' "$name" "$calls" - "$limit" "FAIL (build)"
        bad=1
        continue
    fi

    t0=$(now)
    "${CAP[@]}" timeout 300 "${NVC[@]}" -r top +N=1 > run0.log 2>&1
    t1=$(now)
    "${CAP[@]}" timeout 300 "${NVC[@]}" -r top +N="$calls" > run1.log 2>&1
    rc=$?
    t2=$(now)

    fails=$(sed -n 's/^RES fails=\([0-9]*\)\/.*/\1/p' run1.log | head -1)
    per=$(python3 -c "print('%.2f' % (max(0.0, (($t2 - $t1) - ($t1 - $t0))) * 1e6 / max(1, $calls - 1)))")
    if [ "$calls" -lt "$SOLVER_BELOW" ]; then
        # The run time of a few calls also holds the interpreter running
        # the code that builds the constraints and, after a few dozen
        # calls, the compilation of it, which is longer than the solver
        # for a big constraint.  The time of the solver itself is counted
        # by NVC_CRV_STATS.
        rm -f stats.tsv
        NVC_CRV_STATS="$d/stats.tsv" "${CAP[@]}" timeout 300 "${NVC[@]}" -r top +N="$calls" > run2.log 2>&1
        solver=$(awk -F'\t' '{ b += $26; s += $27; c += $2 }
                             END { if (c > 0) printf "%.2f", (b + s) / c }' stats.tsv 2>/dev/null)
        [ -n "$solver" ] && per=$solver
    fi
    over=$(python3 -c "print(1 if $per > $limit * $SLACK else 0)")

    broken=0
    [ $rc -ne 0 ] && broken=1
    [ -z "$fails" ] && broken=1
    problem=0
    [ $over = 1 ] && problem=1
    [ "${fails:-1}" != 0 ] && problem=1

    case "$expect" in
        pass)
            if [ $broken = 1 ]; then
                result="FAIL (run rc=$rc)"; bad=1
            elif [ $problem = 1 ]; then
                result="FAIL (over limit or ${fails:-?} give-ups)"; bad=1
            else
                result=ok
            fi ;;
        xfail:*)
            if [ $broken = 0 ] && [ $problem = 0 ]; then
                result="XPASS (was ${expect#xfail:})"
            else
                result="xfail (${expect#xfail:}, give-ups ${fails:-?})"
            fi ;;
        *) result="bad expectation '$expect' in limits.txt"; bad=1 ;;
    esac
    printf '%-16s %8s %12s %12s  %s\n' "$name" "$calls" "$per" "$limit" "$result"
done < "$HERE/limits.txt"

exit $bad
