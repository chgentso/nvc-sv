set -xe

! nvc -a $TESTDIR/regress/pstring_quote1.vhd 2>err
grep -q "cannot contain" err
