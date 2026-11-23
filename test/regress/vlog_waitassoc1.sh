set -xe

which nvc

# The missing key is reported once, not each time the condition is checked
nvc --std=19 -a $TESTDIR/regress/vlog_waitassoc1.sv -e vlog_waitassoc1 -r >out 2>err
cat out
grep -q "^40: flag woke" out
grep -q "^70: count woke" out
test "$(grep -c 'no entry' err)" -le 2
