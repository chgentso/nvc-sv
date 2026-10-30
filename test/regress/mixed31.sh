set -xe

nvc -a $TESTDIR/regress/mixed31.v
nvc --std=2008 -a $TESTDIR/regress/mixed31.vhd
nvc --std=2008 -e mixed31
nvc --std=2008 -r mixed31 | grep PASSED || true
nvc --std=2008 -r mixed31 2>&1 | grep -q PASSED
