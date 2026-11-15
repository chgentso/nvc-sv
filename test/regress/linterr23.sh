set -xe

which nvc

# Skipping a broken item must not look at a token that has not been read
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr23.sv 2>lint; then exit 1; fi
grep -q "unexpected (" lint
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
