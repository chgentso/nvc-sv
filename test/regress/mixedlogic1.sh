set -xe

which nvc

# Output and inout logic ports of a Verilog module driven by continuous
# assignments seen from a VHDL design
nvc --std=19 -a $TESTDIR/regress/mixedlogic1v.sv $TESTDIR/regress/mixedlogic1t.vhd -e mixedlogic1 -r 2>&1 | tee out
grep -q "done" out
if grep -qi "assertion\|fatal" out; then exit 1; fi
