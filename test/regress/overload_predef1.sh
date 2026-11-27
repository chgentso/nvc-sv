set -xe

! nvc --std=2008 -a $TESTDIR/regress/overload_predef1.vhd 2>err
grep -q "ambiguous call to function TO_STRING" err
