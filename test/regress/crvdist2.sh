set -xe

nvc --std=2019 -a $TESTDIR/regress/crvdist_pkg.sv $TESTDIR/regress/crvdist2.sv -e crvdist2

# Record of the search alone: each of these is far from uniform
# over the solutions, with chi-square per degree of freedom well above one
NVC_CRV_ENGINE=dfs nvc -r crvdist2 > out.txt
cat out.txt
test $(grep -c '^REPORT' out.txt) -eq 4
awk '/^REPORT/ { for (i = 1; i <= NF; i++) if ($i == "chi2/df") { if ($(i+1) < 3) bad = 1 } }
     END { exit bad }' out.txt
exit 0
