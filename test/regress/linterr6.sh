set -xe

which nvc

# Errors in the preprocessor must not crash the checks of the module
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr6.sv 2>lint; then exit 1; fi
grep -q "cannot find linterr6_missing.vh" lint
! grep -q 'Assertion\|Caught signal' lint
