set -xe

! nvc --std=1993 -a $TESTDIR/regress/conv_sigparam1.vhd 2>err
grep -q "must be a globally static expression or static signal name" err
