set -xe

# The statement probes are asked for when the design is elaborated, not when
# it is run: the test plugin of vhpi30 stops at lines of the design and
# expects the code to have probes (it also checks vpi_nvc_stmt_breakpoint_add)
nvc -a $TESTDIR/regress/vhpi30.vhd
nvc -e --debug-stmts vhpi30
TEST_NAME=vhpi30 nvc -r --load=$(dirname $(which nvc))/../lib/vhpi_test.so vhpi30
