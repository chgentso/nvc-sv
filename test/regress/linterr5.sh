set -xe

which nvc

# A file without errors never gets the note
nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr4b.sv 2>lint
grep -q "lint:width" lint
if grep -q 'lint:partial' lint; then exit 1; fi
