set -xe

! nvc -a $TESTDIR/regress/alloc_res1.vhd 2>err
grep -q "allocator subtype indication cannot include a resolution indication" err
