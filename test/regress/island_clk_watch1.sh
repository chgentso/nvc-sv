#!/bin/bash -ex
set -ex
#
# A signal that an island only uses as a flip-flop clock gets no island input
# watch: of the five instances in vhdl_island_clkwatch1 only the one that
# reads the clock as data and the one that gates it keep a watch, and the
# asynchronous reset keeps its own.
#

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT
export XDG_CACHE_HOME="$WORKDIR/cache"
mkdir -p "$XDG_CACHE_HOME"
cd "$WORKDIR"

export NVC_RTL_ISLANDS=hier

nvc --std=2008 -a $TESTDIR/regress/vhdl_island_clkwatch1.vhd \
    -e vhdl_island_clkwatch1

NVC_ISLAND_WIRE_STATS=1 nvc --std=2008 -r vhdl_island_clkwatch1 >stats.txt 2>&1
cat stats.txt

grep -E '^ISLAND_WIRE: islands=6 ' stats.txt
grep -E '^ISLAND_WIRE_TOP: .* n=2 w=1 in WORK.VHDL_ISLAND_CLKWATCH1.CLK\[0\+1\] ' stats.txt
grep -E '^ISLAND_WIRE_TOP: .* n=1 w=1 in WORK.VHDL_ISLAND_CLKWATCH1.RST\[0\+1\] ' stats.txt
# The clock that nothing but one counter reads has no input watch at all
! grep -E 'in WORK.VHDL_ISLAND_CLKWATCH1.FCLK' stats.txt

# The same run with the islands off gives the same checks
nvc --std=2008 -e --rtl-islands=off vhdl_island_clkwatch1 -r
