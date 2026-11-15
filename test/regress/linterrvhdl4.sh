set -xe

which nvc

# A process that is not closed: the process after it is still checked, and
# junk between two units does not end the file
! nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterrvhdl4.vhd 2>lint
grep -q "linterrvhdl4.vhd:10:.*lint:latch" lint
grep -q "linterrvhdl4.vhd:17:.*unexpected identifier" lint || grep -q "linterrvhdl4.vhd:15:.*unexpected identifier" lint
grep -c "lint:partial" lint
if grep -q 'Assertion\|Caught signal' lint; then exit 1; fi
