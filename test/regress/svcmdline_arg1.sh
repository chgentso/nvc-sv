set -xe

nvc -a $TESTDIR/regress/svcmdline_arg1.sv -e svcmdline_arg1
nvc -r svcmdline_arg1 +FOO=bar | grep -q "OK n="
