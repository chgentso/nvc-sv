set -xe

nvc -a $TESTDIR/regress/sig_rec_agg1.vhd
nvc -e sig_rec_agg1
nvc -r sig_rec_agg1

nvc -e sig_rec_agg1_bad
! nvc -r sig_rec_agg1_bad 2>err
grep -q "value length 2 does not match signal V length 3" err
