set -xe

nvc -a $TESTDIR/regress/vlog_genstats1.sv -e --stats vlog_genstats1 >out
cat out
grep "Verilog instance scopes  5" out
grep "Generate block scopes    13" out
grep "For-generate loops       5 copied, 0 shared" out

nvc -r --stats vlog_genstats1 >out 2>&1
cat out
grep "PASSED" out
grep "Note: setup:.* compiled:" out
