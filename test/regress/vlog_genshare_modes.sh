set -xe

# Every test of generate blocks shared by the iterations of a loop gives
# the same output when each iteration has its own body

for t in vlog_genshare1 vlog_genshare2 vlog_genshare3; do
  nvc -a $TESTDIR/regress/$t.sv
  NVC_GEN_SHARE=off nvc -e $t -r 2>&1 | grep -v "^\*\* Debug:" > $t.off
  NVC_GEN_SHARE=on nvc -e $t -r 2>&1 | grep -v "^\*\* Debug:" > $t.on
  diff -u $t.off $t.on
  grep PASSED $t.on || grep "g\[3\].odd 3" $t.on
done
