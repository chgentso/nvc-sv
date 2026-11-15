set -xe

which nvc

# A bad initialiser does not swallow the declarations after it, so the
# checks of the rest of the module still run
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr8.sv 2>lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 1
grep -q "linterr8.sv:2:.*unexpected ;" lint
grep -q "linterr8.sv:6:.*latch inferred for 'a8'.*\[lint:latch\]" lint
grep -q "linterr8.sv:7:.*truncates 8 bits to 4 bits \[lint:width\]" lint
