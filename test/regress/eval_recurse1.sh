set -xe

nvc --std=2008 -a $TESTDIR/regress/eval_recurse1.vhd
nvc --std=2008 -e eval_recurse1
