set -xe

nvc -a $TESTDIR/regress/vlog_gencache1.sv

# Each iteration with its own body: 100 iterations of u, then 8 + 8 + 1
# for each cell and 4 + 4 + 1 for c2 but the blocks of cell are lowered once
NVC_GEN_SHARE=off nvc -e --stats vlog_gencache1 >out
cat out
grep "Generate block scopes    1809" out
grep "Generate block bodies    126" out

NVC_GEN_SHARE=off nvc -r vlog_gencache1 >out 2>&1
cat out
grep "PASSED" out

# Loops shared by their iterations: u, then g, even, odd and par for each
# of the two bodies of cell
nvc -e --stats vlog_gencache1 >out
cat out
grep "Generate block scopes    1809" out
grep "Generate block bodies    9" out

nvc -r vlog_gencache1 >out 2>&1
cat out
grep "PASSED" out
