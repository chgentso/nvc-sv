set -xe

# A combinational process that calls a function that may report a warning
# (here the metavalue warnings of the numeric_std relational operators)
# runs in an RTL island exactly when it would run unfused: not on an event
# on a bit outside the slice of a signal it reads, but on every event on a
# signal of its sensitivity list, even one that only a branch that is
# never taken reads.  The output must be identical to --rtl-islands=off.

cat >warn1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn_dut is
    port (
        clk : in  std_logic;
        q   : out unsigned(3 downto 0);
        o   : out std_logic );
end entity;

architecture rtl of warn_dut is
    signal a : unsigned(3 downto 0) := "0001";
    signal x : unsigned(3 downto 0);   -- Never driven: all 'U'
    signal b : unsigned(3 downto 0);
    signal c : unsigned(3 downto 0) := "0000";
    signal y : std_logic;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            a(0) <= not a(0);
            c <= c + 1;
        end if;
    end process;

    -- Reads only a slice of a: an event on a(0) does not wake it
    c_slice : b <= a(3 downto 1) & '0' when a(3 downto 1) /= x(2 downto 0)
                   else "1111";

    -- Sensitive to c, which only a branch that is never taken reads:
    -- every event on c wakes it
    p_dead : process (x, c) is
    begin
        if x = "0000" then
            y <= '1';
        elsif unsigned'("0011") >= unsigned'("1111") then
            y <= c(0);
        else
            y <= '0';
        end if;
    end process;

    q <= b;
    o <= y;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn_tb is
end entity;

architecture test of warn_tb is
    signal clk : std_logic := '0';
    signal q   : unsigned(3 downto 0);
    signal o   : std_logic;
begin
    dut : entity work.warn_dut port map (clk, q, o);

    clk <= not clk after 5 ns when now < 40 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -eq 6

nvc --std=2008 --work=w_b -a warn1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn_tb \
    > b_dump.log
grep -q 'WARN_TB.DUT islands=1 fused_procs=5/5' b_dump.log
nvc --std=2008 --work=w_b -r warn_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast warn_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r warn_tb > bna.log 2>&1

nvc --std=2008 --work=w_s -a warn1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e warn_tb
nvc --std=2008 --work=w_s -r warn_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf bna s; do
    diff -u off.log $m.log
done
