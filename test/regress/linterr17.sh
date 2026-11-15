set -xe

which nvc

# A port list that lost its opening bracket gives the one error: the
# declarations of the ports are not reported as missing from the list
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr17.sv 2>lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 1
grep -q "linterr17.sv:2:.*unexpected input" lint
