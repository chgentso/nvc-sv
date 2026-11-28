set -xe

# The predefined "and", "or", "nand" and "nor" of boolean and bit evaluate
# their right operand only when the left one does not decide the result
# (LRM 9.2.2).  An RTL island must not evaluate a right operand that may
# report something, here a numeric_std comparison with a metavalue, when
# the process unfused would not.  The output must be identical to
# --rtl-islands=off.

cat >short1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity short_dut is
    port (
        clk : in  std_logic;
        y   : out std_logic_vector(2 downto 0) );
end entity;

architecture rtl of short_dut is
    signal cnt : unsigned(2 downto 0) := "000";
    signal x   : unsigned(2 downto 0);   -- Never driven: all 'U'
    signal en  : std_logic := '0';
    signal r   : std_logic_vector(2 downto 0);
begin
    p_cnt : process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
            en <= not en;
        end if;
    end process;

    -- The right operand of "and" and "or" is evaluated only when the left
    -- one does not decide the result: the comparison with a metavalue
    -- warns only then
    c_and : r(0) <= '1' when en = '1' and cnt < x else '0';

    p_or : process (en, cnt, x) is
    begin
        if en = '1' or cnt >= x then
            r(1) <= '1';
        else
            r(1) <= '0';
        end if;
    end process;

    p_nand : process (en, cnt, x) is
        variable b : boolean;
    begin
        b := (en = '1') nand (cnt = x);
        r(2) <= '1' when b else '0';
    end process;

    y <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity short_tb is
end entity;

architecture test of short_tb is
    signal clk : std_logic := '0';
    signal y   : std_logic_vector(2 downto 0);
begin
    dut : entity work.short_dut port map (clk, y);

    clk <= not clk after 5 ns when now < 40 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a short1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e short_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -eq 7

nvc --std=2008 --work=w_b -a short1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e short_tb \
    > b_dump.log
grep -q 'SHORT_TB.DUT islands=1 fused_procs=5/5' b_dump.log
nvc --std=2008 --work=w_b -r short_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast short_tb > bnf.log 2>&1

nvc --std=2008 --work=w_s -a short1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e short_tb
nvc --std=2008 --work=w_s -r short_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf s; do
    diff -u off.log $m.log
done
