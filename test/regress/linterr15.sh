set -xe

which nvc

# A syntax error outside the processes does not stop the checks that
# compare them
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr15.sv 2>lint; then exit 1; fi
grep -q "linterr15.sv:[45]:.*lint:blocking" lint
grep -q "linterr15.sv:[45]:.*lint:multidriven" lint
