set -xe

nvc --std=2019 -a $TESTDIR/regress/crvsat1.sv -e crvsat1

# Auto pipeline: SAT fallback answers the hard cases with zero give-ups and
# proves the prime-factorization problem unsatisfiable without warning.
NVC_CRV_ENGINE=auto NVC_CRV_STATS=$PWD/auto.tsv nvc -r crvsat1 > auto.txt 2> auto.err
cat auto.txt
grep -q 'crvsat1 done' auto.txt
! grep -q 'gave up' auto.err
# Column 10 is giveups (must be 0 everywhere); column 15 is components answered by sat
awk -F'\t' '$10 != 0 { bad = 1 } { s += $15 } END { exit (bad || s < 30) }' auto.tsv

# Forced SAT engine answers every satisfiable call site on its own
NVC_CRV_ENGINE=sat NVC_CRV_STATS=$PWD/sat.tsv nvc -r crvsat1 > sat.txt 2> sat.err
grep -q 'crvsat1 done' sat.txt
! grep -q 'gave up' sat.err
awk -F'\t' '$10 != 0 { bad = 1 } { s += $15 } END { exit (bad || s < 100) }' sat.tsv
exit 0
