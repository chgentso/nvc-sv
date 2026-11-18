#!/bin/sh
#
# Browse the components of the UVM demo through the VPI class model.
#
#   uvm-demo.sh BUILD-DIR UVM-SRC DEMO-DIR [separate]
#
# BUILD-DIR is a build tree of nvc with the check programs built, UVM-SRC the
# folder with uvm_pkg.sv and DEMO-DIR the folder with alu_fifo.vhd and
# alu_tb.sv.  With "separate" the UVM package is analysed into a library of
# its own and the test bench is analysed against it.

set -e

B=$1; U=$2; D=$3; MODE=$4
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cd "$W"

NVC="$B/bin/nvc-sv -L $B/lib"
OPTS="--std=2019 -a --relaxed -DUVM_NO_DPI -I$U"

if [ "$MODE" = separate ]; then
    $NVC --std=2019 --work=uvm -a --relaxed -DUVM_NO_DPI -I"$U" "$U/uvm_pkg.sv" > a0.log 2>&1
    NVC="$NVC -L ."
    $NVC $OPTS "$D/alu_fifo.vhd" "$D/alu_tb.sv" > a.log 2>&1
else
    $NVC $OPTS "$U/uvm_pkg.sv" "$D/alu_fifo.vhd" "$D/alu_tb.sv" > a.log 2>&1
fi

$NVC --std=2019 -e tb_top > e.log 2>&1
TEST_NAME=vpicls5 $NVC --std=2019 -r tb_top --load="$B/lib/vpi_test.so" > r.log 2>&1 || true
grep -E 'PASSED|FAILED' r.log
grep -q PASSED r.log
