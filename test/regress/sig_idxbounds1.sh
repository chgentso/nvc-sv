set -xe

nvc -a $TESTDIR/regress/sig_idxbounds1.vhd

! nvc -e sig_idxbounds1 2>err1
grep -q "index 0 outside of INTEGER range 1 to" err1

nvc -e sig_idxbounds1_alloc
! nvc -r sig_idxbounds1_alloc 2>err2
grep -q "index 0 outside of INTEGER range 1 to" err2
