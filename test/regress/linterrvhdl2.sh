set -xe

which nvc

# A bad initialiser does not hide the processes of the architecture
! nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterrvhdl2.vhd 2>lint
test "$(grep -c ': error:' lint)" = 1
grep -q "linterrvhdl2.vhd:7:.*lint:sensitivity" lint
grep -q "linterrvhdl2.vhd:7:.*latch inferred for signal T" lint

# A bad expression in a process gives the one error and keeps the rest
! nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterrvhdl3.vhd 2>lint3
test "$(grep -c ': error:' lint3)" = 1
grep -q "linterrvhdl3.vhd:9:.*lint:sensitivity" lint3
