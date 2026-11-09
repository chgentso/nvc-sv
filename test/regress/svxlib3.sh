set -x

# Interfaces and modules in another library are found through the
# library search path
nvc --std=2019 --work=mylib -a $TESTDIR/regress/svxlib3_if.sv || exit 1
nvc --std=2019 -L . --work=work -a $TESTDIR/regress/svxlib3.sv || exit 1
nvc --std=2019 -L . --work=work -e svxlib3 -r > run.log 2>&1 || { cat run.log; exit 1; }
cat run.log
grep -q "PASS 3c" run.log || exit 1
exit 0
