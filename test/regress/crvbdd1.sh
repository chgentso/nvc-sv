set -xe

nvc --std=2019 -a $TESTDIR/regress/crvdist_pkg.sv $TESTDIR/regress/crvbdd1.sv -e crvbdd1

# The forced stage answers all four call sites, none failed or gave up
NVC_CRV_ENGINE=bdd NVC_CRV_STATS=$PWD/stats.tsv nvc -r crvbdd1 > out.txt
cat out.txt
grep -q 'crvbdd1 done' out.txt
test $(wc -l < stats.tsv) -eq 4
# columns 9 and 10 are failures and give-ups, 13 and 14 the components
# answered by bdd and by dfs, 38 to 40 (bdd) declines are all zero
awk -F'\t' '$9 == 0 && $10 == 0 && $13 >= $2 && $14 == 0 { n++ } END { exit n != 4 }' stats.tsv
awk -F'\t' '{ for (i = 28; i <= 37; i++) if ($i != 0) bad = 1 } END { exit bad }' stats.tsv

# In auto the search is never needed: the components are answered by the
# rejection or the bdd stage (columns 12 and 13)
rm -f auto.tsv
NVC_CRV_ENGINE=auto NVC_CRV_STATS=$PWD/auto.tsv nvc -r crvbdd1 > auto.txt
grep -q 'crvbdd1 done' auto.txt
awk -F'\t' '$9 == 0 && $10 == 0 && $14 == 0 && $12 + $13 >= $2 { n++ } END { exit n != 4 }' auto.tsv
awk -F'\t' '$13 > 0 { n++ } END { exit n < 3 }' auto.tsv
exit 0
