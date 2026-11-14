set -xe

which nvc

# Names that could not be resolved because of an earlier error must not
# crash the checks of the rest of the file
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr12.sv 2>lint; then exit 1; fi
grep -q "unexpected typedef while parsing package declaration" lint
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
