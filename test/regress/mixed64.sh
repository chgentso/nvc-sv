set -xe

nvc --std=2008 -a $TESTDIR/regress/mixed64.sv $TESTDIR/regress/mixed64.vhd -e mixed64 -r 2>&1 | tee out
grep -q "PASSED" out
if grep -qi "failure\|fatal" out; then exit 1; fi
