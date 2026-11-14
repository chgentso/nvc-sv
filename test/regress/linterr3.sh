set -xe

which nvc

# Findings before and after a syntax error are kept, none inside the
# damaged lines, and no implicit net in a unit with a syntax error
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr3.sv 2>lint; then exit 1; fi
grep -q "linterr3.sv:2:.*continuous assignment truncates 4 bits to 2 bits \[lint:width\]" lint
grep -q "linterr3.sv:6:.*continuous assignment truncates 4 bits to 2 bits \[lint:width\]" lint
if grep -q "linterr3.sv:[34]:.*lint:width" lint; then exit 1; fi
if grep -q 'lint:implicit-net' lint; then exit 1; fi
grep -q 'lint:partial' lint
