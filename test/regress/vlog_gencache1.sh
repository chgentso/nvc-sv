set -xe

nvc -a $TESTDIR/regress/vlog_gencache1.sv -e --stats vlog_gencache1 >out
cat out
# 100 iterations of u, then 8 + 8 + 1 for each cell and 4 + 4 + 1 for c2
# but the blocks of cell are lowered once
grep "Generate block scopes    1809" out
grep "Generate block bodies    126" out

nvc -r vlog_gencache1 >out 2>&1
cat out
grep "PASSED" out
