set -xe

nvc --std=2019 -a $TESTDIR/regress/crvgiveup1.sv -e crvgiveup1

# With the usual limit the problem is solved and nothing is said
nvc -r crvgiveup1 > full.txt 2> full.err
grep -qx ok full.txt
! grep -q gave full.err

NVC_CRV_BUDGET=100 nvc -r crvgiveup1 > small.txt 2> small.err
grep -qx 'no solution' small.txt
grep -q 'randomize() gave up without finding a solution' small.err
grep -q 'crvgiveup1.sv:13' small.err
