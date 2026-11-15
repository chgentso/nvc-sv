set -xe

which nvc

# Rules that compare blocks only use the blocks before a syntax error in a
# process: a block after it may be missing part of what it was meant to hold
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr13.sv 2>lint; then exit 1; fi
if grep -q 'lint:blocking\|lint:multidriven' lint; then exit 1; fi
grep -q 'lint:partial' lint
