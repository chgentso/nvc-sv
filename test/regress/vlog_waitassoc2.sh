set -xe

which nvc

nvc --std=19 -a $TESTDIR/regress/vlog_waitassoc2.sv -e vlog_waitassoc2 -r >out 2>err
cat out
grep -q "^40: alert fatal_fault woke" out
grep -q "^45: tag fatal_fault woke" out
grep -q "^50: dyn_key woke" out
grep -q "^55: mut_key woke" out
grep -q "^60: deleted woke" out
grep -q "^65: cleared woke" out
grep -q "^70: reassign woke" out
grep -q "^80: guard_and woke" out
grep -q "^85: guard_or woke" out
grep -q "^90: fn_key woke" out
grep -q "^95: unwatch_rhs woke" out
grep -q "^PASSED" out
test "$(grep -c 'no entry' err)" -le 10
