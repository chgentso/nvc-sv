set -x

# A component declared in a package of another library is bound to the
# Verilog module of the same name in that library
nvc --std=2008 --work=xl:xl -a $TESTDIR/regress/svxlib1_m.sv $TESTDIR/regress/svxlib1_pkg.vhd || exit 1
nvc --std=2008 -L . --work=work -a $TESTDIR/regress/svxlib1.vhd || exit 1
nvc --std=2008 -L . --work=work -e svxlib1 -r > run.log 2>&1 || { cat run.log; exit 1; }
cat run.log
grep -q "5A" run.log || exit 1
exit 0
