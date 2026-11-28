set -xe

# A report from a function that an RTL island calls, such as an IEEE
# metavalue warning, names the fused process in its stack trace and takes
# the place of that process in the output of the delta cycle (see
# rorder.c), as with --rtl-islands=off: the island code carries the
# location of each statement.

cat >trace1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity trace_dut is
    port (
        clk : in  std_logic;
        q   : out std_logic_vector(1 downto 0) );
end entity;

architecture rtl of trace_dut is
    signal cnt : unsigned(3 downto 0) := "0000";
    signal x   : unsigned(3 downto 0);   -- Never driven: all 'U'
    signal y   : std_logic_vector(1 downto 0);
begin
    p_cnt : process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
        end if;
    end process;

    -- Both compare with a metavalue on every change of cnt: the warnings
    -- name each process and come out in the order of the processes
    c_lo : y(0) <= '1' when cnt < x else '0';

    p_hi : process (cnt, x) is
    begin
        if cnt >= x then
            y(1) <= '1';
        else
            y(1) <= '0';
        end if;
    end process;

    -- Not fused: reports in the same delta cycles
    p_mon : process (cnt) is
    begin
        report "cnt=" & to_string(cnt);
    end process;

    q <= y;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity trace_tb is
end entity;

architecture test of trace_tb is
    signal clk : std_logic := '0';
    signal q   : std_logic_vector(1 downto 0);
begin
    dut : entity work.trace_dut port map (clk, q);

    clk <= not clk after 5 ns when now < 30 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a trace1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e trace_tb -r > off.log 2>&1
grep -q 'Process :trace_tb:dut:p_hi' off.log

nvc --std=2008 --work=w_b -a trace1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e trace_tb \
    > b_dump.log
grep -q 'TRACE_TB.DUT islands=1 fused_procs=4/5' b_dump.log
nvc --std=2008 --work=w_b -r trace_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast trace_tb > bnf.log 2>&1

nvc --std=2008 --work=w_s -a trace1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e trace_tb
nvc --std=2008 --work=w_s -r trace_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf s; do
    diff -u off.log $m.log
done
