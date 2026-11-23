set -xe

nvc --std=2019 -a $TESTDIR/regress/crvdist_pkg.sv $TESTDIR/regress/crvdist1.sv -e crvdist1
nvc -r crvdist1 > out.txt
cat out.txt
grep -q 'crvdist1 done' out.txt
! grep -q FAIL out.txt
grep -c PASS out.txt | grep -qx 3

# The same with the search alone
NVC_CRV_ENGINE=dfs nvc -r crvdist1 > dfs.txt
grep -q 'crvdist1 done' dfs.txt
exit 0
