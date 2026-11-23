set -xe

# One body for all iterations of a large loop
nvc -a $TESTDIR/regress/vlog_genshare10.sv
NVC_GEN_SHARE=on nvc -e --stats vlog_genshare10 > stats 2>&1
cat stats
grep "Generate block scopes    10000" stats
grep "Generate block bodies    1$" stats
grep "For-generate loops       0 copied, 1 shared" stats
NVC_GEN_SHARE=on nvc -r --stats vlog_genshare10 > run 2>&1
cat run
grep PASSED run
compiled=$(sed -n 's/.* compiled:\([0-9]*\) .*/\1/p' run)
[ "$compiled" -lt 10300 ]

# Coverage keeps one body for each iteration
nvc -a $TESTDIR/regress/vlog_genshare1.sv
NVC_GEN_SHARE=on nvc -e --cover=statement --stats vlog_genshare1 > cover 2>&1
grep "For-generate loops       13 copied, 0 shared" cover
