set -xe

nvc --std=2008 -a $TESTDIR/regress/rec_arr_agg1.vhd
! nvc --std=2008 -e rec_arr_agg1 2>err
grep -q "does not match" err
