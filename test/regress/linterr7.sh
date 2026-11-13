set -xe

which nvc

# A name that comes from a package that was not found is not an implicit net
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr7.sv 2>lint; then exit 1; fi
grep -q "linterr7_missing_pkg" lint
if grep -q 'lint:implicit-net' lint; then exit 1; fi
