set -xe

! nvc -a --relaxed $TESTDIR/regress/attr_ent_ext1.vhd 2>err
grep -q "cannot read non-static attribute TAG of entity" err
