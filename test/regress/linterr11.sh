set -xe

which nvc

# A declaration swallowed by a syntax error leaves its name an implicit
# net: assignments to such names are not checked
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr11.sv 2>lint; then exit 1; fi
if grep -q 'lint:width' lint; then exit 1; fi
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
