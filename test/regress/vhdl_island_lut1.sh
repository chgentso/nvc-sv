set -xe

# The std_logic operator tables of an island function are built once per
# function.  They must be built in a block that dominates every use: here
# the first use of the xor table is in a branch that MIR optimisation
# removes, and the later uses in other processes and in the clock branch
# of the flip-flop crashed GVN ("node has no value number").  The output
# must be identical to --rtl-islands=off.

cat >lut1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity lut_dut is
    port (
        clk, rst  : in  std_logic;
        a, b, sel : in  std_logic;
        y, z, q   : out std_logic );
end entity;

architecture rtl of lut_dut is
    signal r, w : std_logic := '0';
begin
    -- The first use of the xor table is in a branch that folds away
    p_comb : process (all) is
    begin
        if (std_logic'('1') /= std_logic'('1')) and (a = b) then
            w <= a xor b;
        elsif sel = '1' then
            w <= a xnor b;
        else
            w <= a and b;
        end if;
    end process;

    -- Uses the tables again in a later process
    p_comb2 : process (all) is
    begin
        if sel = '0' then
            z <= (a xor sel) or w;
        else
            z <= (a and b) xor r;
        end if;
    end process;

    -- The reset and clock branches share the tables
    p_ff : process (clk, rst) is
    begin
        if rst = '1' then
            r <= a xor b;
        elsif rising_edge(clk) then
            r <= (a xor r) and (b or sel);
        end if;
    end process;

    q <= r;
    y <= w;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity lut_tb is
end entity;

architecture test of lut_tb is
    signal clk, rst, a, b, sel, y, z, q : std_logic := '0';
begin
    dut : entity work.lut_dut port map (clk, rst, a, b, sel, y, z, q);

    p_log : process (y, z, q) is
    begin
        report "y=" & to_string(y) & " z=" & to_string(z)
            & " q=" & to_string(q);
    end process;

    p_stim : process is
    begin
        rst <= '1';
        wait for 1 ns;
        rst <= '0';
        for k in 0 to 15 loop
            a <= '1' when k mod 2 = 1 else '0';
            b <= '1' when k mod 4 >= 2 else '0';
            sel <= '1' when k mod 8 >= 4 else '0';
            wait for 1 ns;
            clk <= '1';
            wait for 1 ns;
            clk <= '0';
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a lut1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e lut_tb -r > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a lut1.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e lut_tb
nvc --std=2008 --work=w_b -r lut_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast lut_tb > bnf.log 2>&1

nvc --std=2008 --work=w_s -a lut1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e lut_tb
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r lut_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf s; do
    diff -u off.log $m.log
done
