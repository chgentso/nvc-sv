set -xe

which nvc

# A process in a unit with errors is analysed as the parser left it, not as
# the simplifier would: what is assigned under a condition on a loop
# parameter is not reported as a latch
! nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterrvhdl7.vhd 2>lint
! grep -q 'lint:latch' lint
grep -q 'lint:partial' lint

# The same process in a unit without errors is not a latch either
sed 's/^end entity$/end entity;/' $TESTDIR/regress/linterrvhdl7.vhd > fixed.vhd
nvc --std=19 --lint --messages=compact -a fixed.vhd 2>clean
! grep -q 'lint:latch' clean
