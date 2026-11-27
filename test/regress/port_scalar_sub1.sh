set -xe

nvc -a $TESTDIR/regress/port_scalar_sub1.vhd

nvc -e port_scalar_sub1_ok
nvc -r port_scalar_sub1_ok

! nvc -e port_scalar_sub1_bad_in 2>err1
grep -q "value -5 outside of NATURAL range" err1

! nvc -e port_scalar_sub1_bad_out 2>err2
grep -q "outside of NATURAL range" err2
