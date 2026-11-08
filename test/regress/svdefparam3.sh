set -x

nvc -a $TESTDIR/regress/svdefparam3.sv || exit 1
nvc -e svdefparam3 > elab.log 2>&1 && exit 1
cat elab.log
grep -q "cannot find 'nosuch' in hierarchical identifier 'nosuch.A'" elab.log || exit 1
grep -q "cannot find 'NOPE' in hierarchical identifier 'l.NOPE'" elab.log || exit 1
exit 0
