set -xe

# Every test of generate blocks shared by the iterations of a loop gives
# the same output when each iteration has its own body

for t in vlog_genshare1 vlog_genshare2 vlog_genshare3 vlog_genshare4 \
         vlog_genshare6 vlog_genshare7 vlog_genshare9 vlog_genshare11 \
         vlog_genshare12; do
  nvc -a $TESTDIR/regress/$t.sv
  NVC_GEN_SHARE=off nvc -e $t -r 2>&1 | grep -v "^\*\* Debug:" > $t.off
  NVC_GEN_SHARE=on nvc -e $t -r 2>&1 | grep -v "^\*\* Debug:" > $t.on
  diff -u $t.off $t.on
  grep PASSED $t.on || grep "g\[3\].odd 3" $t.on
done

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
NVC_GEN_SHARE=on nvc -e --cover=statement --stats vlog_genshare1 > cover 2>&1
grep "For-generate loops       13 copied, 0 shared" cover
