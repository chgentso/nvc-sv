set -xe

which nvc

# A missing end or endcase closes the block at the next item instead of
# taking the rest of the module and the next one with it
if nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterr9.sv 2>lint; then exit 1; fi
grep -q "linterr9.sv:17:.*truncates 8 bits to 4 bits \[lint:width\]" lint
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
