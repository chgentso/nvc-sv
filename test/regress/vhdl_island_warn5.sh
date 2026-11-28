set -xe

# Like vhdl_island_warn2: processes that may warn and read short slices
# that do not start or end on a word of the island state (inside one word,
# across two words, exactly one word long) run only on events inside their
# slice.  The output must be identical to --rtl-islands=off.

cat >warn5.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn5_dut is
    port (
        clk : in  std_logic;
        q   : out std_logic_vector(4 downto 0) );
end entity;

architecture rtl of warn5_dut is
    signal v : unsigned(63 downto 0) := (0 => '1', others => '0');
    signal x : unsigned(25 downto 0);   -- Never driven: all 'U'
    signal y : std_logic_vector(4 downto 0);
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            v <= v(62 downto 0) & v(63);   -- A walking one
        end if;
    end process;

    c_in_word : y(0) <= '1' when v(61 downto 58) /= x(3 downto 0) else '0';
    c_across  : y(1) <= '1' when v(52 downto 43) /= x(9 downto 0) else '0';
    c_word    : y(2) <= '1' when v(36 downto 29) /= x(7 downto 0) else '0';
    c_long    : y(3) <= '1' when v(27 downto 2) /= x else '0';
    c_tail    : y(4) <= '1' when v(2 downto 0) /= x(2 downto 0) else '0';

    q <= y;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity warn5_tb is
end entity;

architecture test of warn5_tb is
    signal clk : std_logic := '0';
    signal q   : std_logic_vector(4 downto 0);
begin
    dut : entity work.warn5_dut port map (clk, q);

    clk <= not clk after 5 ns when now < 700 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn5.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn5_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -gt 60

nvc --std=2008 --work=w_b -a warn5.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn5_tb \
    > b_dump.log
grep -q 'WARN5_TB.DUT islands=1 fused_procs=7/7' b_dump.log
nvc --std=2008 --work=w_b -r warn5_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast warn5_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r warn5_tb > bna.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf bna; do
    diff -u off.log $m.log
done
