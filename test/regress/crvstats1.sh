set -xe

nvc --std=2019 -a $TESTDIR/regress/crvstats1.sv -e crvstats1

# The solver's choices do not depend on the statistics or the order of the
# checks: values for a seed are the same with them on, whatever the engine.
# Auto has the rejection stage in front of the search of dfs, which is the
# search of release 0.18.0 with components, so they choose other values
NVC_CRV_ENGINE=auto nvc -r crvstats1 > base.txt
grep -c '^[0-9]' base.txt | grep -qx 30

NVC_CRV_ENGINE=legacy nvc -r crvstats1 > legacy.txt
NVC_CRV_ENGINE=dfs nvc -r crvstats1 > dfs.txt
NVC_CRV_ENGINE=auto nvc -r crvstats1 > auto.txt
NVC_CRV_ENGINE=auto NVC_CRV_STATS=1 nvc -r crvstats1 > stats.txt 2> table.txt
NVC_CRV_ENGINE=auto NVC_CRV_STATS=$PWD/stats.tsv nvc -r crvstats1 > stats2.txt
grep -c '^[0-9]' legacy.txt | grep -qx 30
NVC_CRV_ENGINE=dfs NVC_CRV_STATS=$PWD/dfs.tsv nvc -r crvstats1 > dfs2.txt
cmp dfs.txt dfs2.txt
! cmp -s base.txt dfs.txt
cmp base.txt auto.txt
grep '^[0-9]' stats.txt | cmp - base.txt
cmp base.txt stats2.txt

# A call site is a source line; the table counts the 30 calls and says that
# every one has components of more than one free variable
grep -q 'call sites\|calls per call site' table.txt
grep -q 'crvstats1.sv:26' table.txt
test $(wc -l < stats.tsv) -eq 1
awk -F'\t' '$1 ~ /crvstats1.sv:26/ && $2 == 30 && $6 == 30 && NF == 40 { ok = 1 }
            END { exit !ok }' stats.tsv

# Splitting variables at slice boundaries is only done by the legacy engine,
# with its own switch; the other engines leave the slices to the bdd stage
NVC_CRV_ENGINE=legacy NVC_CRV_STATS=$PWD/legacy.tsv nvc -r crvstats1 > legacy2.txt
awk -F'\t' '$7 > 0 { ok = 1 } END { exit !ok }' legacy.tsv
NVC_CRV_ENGINE=legacy NVC_CRV_DECOMPOSE=0 NVC_CRV_STATS=$PWD/nodec.tsv nvc -r crvstats1 > nodec.txt
awk -F'\t' '$7 == 0 { ok = 1 } END { exit !ok }' nodec.tsv
awk -F'\t' '$7 == 0 { ok = 1 } END { exit !ok }' stats.tsv
NVC_CRV_DECOMPOSE=0 nvc -r crvstats1 > nodec2.txt 2> nodec.err
cmp base.txt nodec2.txt
grep -q 'no effect' nodec.err

# The bdd stage on its own answers every component of the 30 calls, and
# is the stage that answers the slices in auto
NVC_CRV_ENGINE=bdd NVC_CRV_STATS=$PWD/bdd.tsv nvc -r crvstats1 > bdd.txt
grep -c '^[0-9]' bdd.txt | grep -qx 30
awk -F'\t' '$9 == 0 && $10 == 0 && $13 > 0 { ok = 1 } END { exit !ok }' bdd.tsv

# A stage that does not exist yet falls back to the search, with a message
NVC_CRV_ENGINE=sat nvc -r crvstats1 > sat.txt 2> sat.err
cmp dfs.txt sat.txt
grep -q 'not available' sat.err
NVC_CRV_ENGINE=nonsense nvc -r crvstats1 > bad.txt 2> bad.err
cmp base.txt bad.txt
grep -q 'unknown engine' bad.err
exit 0
