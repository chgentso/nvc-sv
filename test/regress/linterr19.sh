set -xe

which nvc

# A unit that has used half of its memory is no longer read on from after a
# syntax error, so that it does not run out of it: there is a note and no
# fatal error
if nvc --std=19 --lint --messages=compact -M 160k -a $TESTDIR/regress/linterr19.sv 2>lint; then exit 1; fi
grep -q "error recovery stopped for the rest of this unit" lint
if grep -q 'memory exhausted' lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 1
