set -xe

which nvc

# Trees that the parser could only half build after a syntax error must not
# crash the checks that look at them
for i in 1 2 3 4 5; do
    nvc --std=19 --lint --messages=compact -a $TESTDIR/regress/linterrvhdl6_$i.vhd 2>lint$i || true
    if grep -q 'Assertion\|Caught signal' lint$i; then exit 1; fi
done
