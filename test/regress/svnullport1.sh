set -x

nvc --std=2008 -a $TESTDIR/regress/svnullport1.vhd $TESTDIR/regress/svnullport1.sv || exit 1

# A fatal error while creating an instance must not crash
nvc --std=2008 -e svnullport1 > elab.log 2>&1 && exit 1
cat elab.log
grep -q "actual length 0 does not match formal length 1" elab.log || exit 1
grep -q "Caught signal" elab.log && exit 1
exit 0
