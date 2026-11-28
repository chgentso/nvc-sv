set -xe

# Like vhdl_island_warn1: a process that may warn and reads a slice of more
# than 64 elements that does not start on a word of the island state runs
# only on events inside the slice.  The output must be identical to
# --rtl-islands=off.

cat >warn2.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn2_dut is
    port (
        clk : in  std_logic;
        q   : out std_logic );
end entity;

architecture rtl of warn2_dut is
    signal v : unsigned(99 downto 0) := (others => '0');
    signal x : unsigned(78 downto 0);   -- Never driven: all 'U'
    signal y : std_logic;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            v(99) <= not v(99);
            v(0) <= not v(0);
            if v(0) = '1' then
                v(50) <= not v(50);
            end if;
        end if;
    end process;

    -- Reads a long slice that does not start on a word of the island
    -- state: an event on v(99) or v(0) does not wake it
    c_long : y <= '1' when v(98 downto 20) /= x else '0';

    q <= y;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity warn2_tb is
end entity;

architecture test of warn2_tb is
    signal clk : std_logic := '0';
    signal q   : std_logic;
begin
    dut : entity work.warn2_dut port map (clk, q);

    clk <= not clk after 5 ns when now < 80 ns;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn2.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn2_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -eq 5

nvc --std=2008 --work=w_b -a warn2.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn2_tb \
    > b_dump.log
grep -q 'WARN2_TB.DUT islands=1 fused_procs=3/3' b_dump.log
nvc --std=2008 --work=w_b -r warn2_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast warn2_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r warn2_tb > bna.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf bna; do
    diff -u off.log $m.log
done
