set -xe

which nvc

# A stray token between two units does not hide the checks of the second
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr20.sv 2>lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 1
grep -q "linterr20.sv:6:.*truncates 8 bits to 4 bits \[lint:width\]" lint
