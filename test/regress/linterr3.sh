set -xe

which nvc

# Findings before a syntax error are kept, none at or after it, and no
# implicit net in a unit with a syntax error
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr3.sv 2>lint; then exit 1; fi
grep -q "linterr3.sv:2:.*continuous assignment truncates 4 bits to 2 bits \[lint:width\]" lint
! grep -q "linterr3.sv:[56]:.*lint:width" lint
! grep -q 'lint:implicit-net' lint
grep -q 'lint:partial' lint
