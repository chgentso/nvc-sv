set -xe

which nvc

# A one bit bit or logic parameter of a Verilog module accepts a bit or
# std_logic literal as well as an integer from VHDL
nvc --std=19 -a $TESTDIR/regress/mixedparam1v.sv $TESTDIR/regress/mixedparam1t.vhd -e mixedparam1 -r 2>&1 | tee out
grep -q "done" out
if grep -qi "failure\|fatal" out; then exit 1; fi

# Only 0 and 1 fit
nvc --std=19 -a $TESTDIR/regress/mixedparam1v.sv $TESTDIR/regress/mixedparam1e.vhd
if nvc --std=19 -e mixedparam1e 2>err; then exit 1; fi
grep -q "outside the range 0 to 1" err
