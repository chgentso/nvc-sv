set -xe

! nvc --std=2008 -a $TESTDIR/regress/nested_pkg_body1.vhd 2>err
grep -q "missing package body for nested package P" err
