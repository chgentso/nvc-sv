set -x

# A package in another library is found through the library search path
nvc --std=2019 --work=mylib -a $TESTDIR/regress/svxlib2_pkg.sv || exit 1
nvc --std=2019 -L . --work=work -a $TESTDIR/regress/svxlib2.sv || exit 1
nvc --std=2019 -L . --work=work -e svxlib2 -r > run.log 2>&1 || { cat run.log; exit 1; }
cat run.log
grep -q "PASS 10" run.log || exit 1

# Without the library on the search path the package is not found
nvc --std=2019 --work=work2 -a $TESTDIR/regress/svxlib2.sv > err.log 2>&1 && exit 1
cat err.log
grep -q "no package named 'mypkg'" err.log || exit 1
grep -q "library search path contains" err.log || exit 1
exit 0
