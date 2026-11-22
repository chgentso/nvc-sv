set -xe

nvc --std=2019 -a $TESTDIR/regress/crvstats1.sv -e crvstats1

# The solver's choices do not depend on the statistics or the order of the
# checks, and dfs and auto are the same search: values for a seed are the
# same.  Legacy is the search of release 0.18.0, which may choose other values
NVC_CRV_ENGINE=auto nvc -r crvstats1 > base.txt
grep -c '^[0-9]' base.txt | grep -qx 30

NVC_CRV_ENGINE=legacy nvc -r crvstats1 > legacy.txt
NVC_CRV_ENGINE=dfs nvc -r crvstats1 > dfs.txt
NVC_CRV_ENGINE=auto nvc -r crvstats1 > auto.txt
NVC_CRV_ENGINE=auto NVC_CRV_STATS=1 nvc -r crvstats1 > stats.txt 2> table.txt
NVC_CRV_ENGINE=auto NVC_CRV_STATS=$PWD/stats.tsv nvc -r crvstats1 > stats2.txt
grep -c '^[0-9]' legacy.txt | grep -qx 30
cmp base.txt dfs.txt
cmp base.txt auto.txt
grep '^[0-9]' stats.txt | cmp - base.txt
cmp base.txt stats2.txt

# A call site is a source line; the table counts the 30 calls and says that
# every one has components of more than one free variable
grep -q 'call sites\|calls per call site' table.txt
grep -q 'crvstats1.sv:26' table.txt
test $(wc -l < stats.tsv) -eq 1
awk -F'\t' '$1 ~ /crvstats1.sv:26/ && $2 == 30 && $6 == 30 && NF == 27 { ok = 1 }
            END { exit !ok }' stats.tsv

# Splitting variables at slice boundaries is a separate switch: with it off
# the values are the solver's own for a problem without the split
NVC_CRV_DECOMPOSE=0 NVC_CRV_STATS=$PWD/nodec.tsv nvc -r crvstats1 > nodec.txt
awk -F'\t' '$7 == 0 { ok = 1 } END { exit !ok }' nodec.tsv
awk -F'\t' '$7 > 0 { ok = 1 } END { exit !ok }' stats.tsv

# A stage that does not exist yet falls back to the search, with a message
NVC_CRV_ENGINE=bdd nvc -r crvstats1 > bdd.txt 2> bdd.err
cmp base.txt bdd.txt
grep -q 'not available' bdd.err
NVC_CRV_ENGINE=nonsense nvc -r crvstats1 > bad.txt 2> bad.err
cmp base.txt bad.txt
grep -q 'unknown engine' bad.err
exit 0
