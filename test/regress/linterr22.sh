set -xe

which nvc

# After a unit was skipped the names that it might have declared are not
# reported as missing
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr22.sv 2>lint; then exit 1; fi
if grep -q 'no data type declaration\|no visible declaration\|must have class type' lint; then exit 1; fi
test "$(grep -c ': error:' lint)" -le 2
