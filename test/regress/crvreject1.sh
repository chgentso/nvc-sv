set -xe

nvc --std=2019 -a $TESTDIR/regress/crvdist_pkg.sv $TESTDIR/regress/crvreject1.sv -e crvreject1

# The default engine samples all of these exactly, by rejection
NVC_CRV_ENGINE=auto NVC_CRV_STATS=$PWD/stats.tsv nvc -r crvreject1 > out.txt
cat out.txt
grep -q 'crvreject1 done' out.txt
! grep -q FAIL out.txt
grep -c PASS out.txt | grep -qx 6

# Every call site was answered by the rejection stage in all but the rare
# call in which all 16 samples are rejected, and none failed or gave up:
# columns 12 and 14 are the components answered by reject and by dfs
test $(wc -l < stats.tsv) -eq 6
awk -F'\t' '$12 >= 0.99 * $2 && $10 == 0 && $9 == 0 { n++ } END { exit n != 6 }' stats.tsv

# The forced stage gives the same kind of answers
NVC_CRV_ENGINE=reject nvc -r crvreject1 > forced.txt
grep -q 'crvreject1 done' forced.txt
! grep -q FAIL forced.txt
exit 0
