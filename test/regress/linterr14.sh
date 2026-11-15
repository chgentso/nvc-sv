set -xe

which nvc

# After an error is recovered from, errors are quiet until an item starts,
# but a second mistake further on is still reported
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr14.sv 2>lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 2
grep -q "linterr14.sv:6:.*unexpected end" lint
grep -q "linterr14.sv:9:.*unexpected ;" lint
