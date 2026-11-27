#!/bin/bash -ex
set -ex
#
# NVC_ISLAND_WIRE_STATS: report why island-written signals are not direct
# inter-island wires, with the input wakeups and syncs each class causes.
#

if [ -n "${ASAN_OPTIONS:-}" ]; then
  export ASAN_OPTIONS="$ASAN_OPTIONS:detect_leaks=0"
fi

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT
export XDG_CACHE_HOME="$WORKDIR/cache"
mkdir -p "$XDG_CACHE_HOME"
cd "$WORKDIR"

cat > wire_stats.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;

package wire_pkg is
  type bus_t is record
    a : std_logic_vector(3 downto 0);
    b : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.wire_pkg.all;

entity producer is
  port (clk : in std_logic; q : out std_logic_vector(7 downto 0);
        bo : out bus_t; lo : out std_logic);
end entity;

architecture rtl of producer is
  signal cnt : std_logic_vector(7 downto 0) := x"00";
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      cnt <= std_logic_vector(unsigned(cnt) + 1);
    end if;
  end process;
  q <= cnt;
  bo.a <= cnt(3 downto 0);
  bo.b <= cnt(7 downto 4);
  lo <= cnt(0);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.wire_pkg.all;

entity consumer is
  port (clk : in std_logic; d : in std_logic_vector(7 downto 0);
        bi : in bus_t; li : in std_logic; y : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of consumer is
  signal r : std_logic_vector(7 downto 0) := x"00";
begin
  process (clk) is
  begin
    if rising_edge(clk) then
      r <= d xor bi.a & bi.b;
    end if;
  end process;
  y <= r when li = '1' else not r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.wire_pkg.all;

entity wire_stats is end entity;

architecture test of wire_stats is
  signal clk : std_logic := '0';
  signal q, y : std_logic_vector(7 downto 0);
  signal b : bus_t;
  signal l : std_logic;
begin
  clk <= not clk after 5 ns when now < 400 ns else clk;
  u1: entity work.producer port map (clk, q, b, l);
  u2: entity work.consumer port map (clk, q, b, l, y);
  process
  begin
    wait for 450 ns;
    report "y=" & to_string(y);
    wait;
  end process;
end architecture;
EOF

export NVC_RTL_ISLANDS=hier

nvc --std=2008 -a wire_stats.vhd -e wire_stats

# Off by default
unset NVC_ISLAND_WIRE_STATS
nvc --std=2008 -r wire_stats >plain.txt 2>&1
if grep -q ISLAND_WIRE plain.txt; then
  echo "unexpected ISLAND_WIRE output"
  exit 1
fi

# Both producer outputs and the record port are wired directly; the
# testbench-driven clock is the only reader without an island writer
export NVC_ISLAND_WIRE_STATS=1
nvc --std=2008 -r wire_stats >stats.txt 2>&1
cat stats.txt
grep -E '^ISLAND_WIRE: islands=2 super_islands=1 ' stats.txt
grep -E '^ISLAND_WIRE: DIRECT +4 +4 +17 ' stats.txt
grep -E '^ISLAND_WIRE: no-consumer \(island-internal\) +2 +0 +16 ' stats.txt
grep -E '^ISLAND_WIRE: dst-port-poll/ext-read +1 +1 +8 ' stats.txt
grep -E '^ISLAND_WIRE: READER<-non-island-process +0 +0 +0 +0 +0 \| +2 ' stats.txt
grep -E '^ISLAND_WIRE_EX: \[dst-port-poll/ext-read\] WORK.WIRE_STATS.U2.Y\[0\+8\] -> WORK.WIRE_STATS.Y$' stats.txt
grep -E '^ISLAND_WIRE_TOP: wake=160 sync_in=80 .* in WORK.WIRE_STATS.CLK\[0\+1\] <-proc ' stats.txt

# A waveform dumper observes every signal, so the direct wires are broken
nvc --std=2008 -r wire_stats --wave=wire_stats.fst >wave.txt 2>&1
cat wave.txt
grep -E '^ISLAND_WIRE: DIRECT -> broken:alias-break +4 +4 +17 ' wave.txt
grep -E '^ISLAND_WIRE: dst-port-poll/ext-read -> broken:observer-attached +1 +1 +8 ' wave.txt
