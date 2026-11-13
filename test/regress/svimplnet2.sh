set -xe

which nvc

# No implicit net with default_nettype none
if nvc --std=19 --messages=compact -a $TESTDIR/regress/svimplnet2.sv 2>err; then exit 1; fi
grep -q "svimplnet2.sv:5:.*no visible declaration for 'led'" err
grep -q "svimplnet2.sv:6:.*no visible declaration for 'led'" err
