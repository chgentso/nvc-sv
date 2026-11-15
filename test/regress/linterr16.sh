set -xe

which nvc

# A target that could not be resolved because of a syntax error must not
# crash the check of a continuous assignment
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr16.sv 2>lint; then exit 1; fi
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
