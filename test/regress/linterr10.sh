set -xe

which nvc

# With --lint the error limit only limits what is printed: the checks of
# the module after it still run
if nvc --std=19 --lint --messages=compact -a --error-limit=10 $TESTDIR/regress/linterr10.sv 2>lint; then exit 1; fi
test "$(grep -c ': error:' lint)" = 10
grep -q "10 errors shown; further errors in this run are not shown" lint
grep -q "linterr10.sv:31:.*truncates 8 bits to 4 bits \[lint:width\]" lint
if grep -q 'too many errors' lint; then exit 1; fi

# Without --lint the limit stops the run as before
if nvc --std=19 --messages=compact -a --error-limit=10 $TESTDIR/regress/linterr10.sv 2>plain; then exit 1; fi
grep -q 'too many errors' plain
