set -xe

nvc --std=2019 -a $TESTDIR/regress/crvsize1.sv -e crvsize1

# Sizes that leave the elements without a solution are not chosen again
NVC_CRV_ENGINE=auto nvc -r crvsize1 | grep -x 'ok=200'
