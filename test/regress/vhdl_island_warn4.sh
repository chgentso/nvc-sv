set -xe

# The run guard of a combinational process that may warn tests the change
# bytes of a group of signals of its sensitivity list before comparing them
# with their copies, and updates the copies in the same pass.  A
# transaction without an event sets the change byte of an island input but
# must not run the process; an event on one element of the group must.
# The output must be identical to --rtl-islands=off.

cat >warn4.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package warn4_pkg is
    type in_t is record
        a, b, c, d, e, f, g, h, i, j : std_logic;
        k : unsigned(2 downto 0);
        v : std_logic_vector(23 downto 0);
    end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.warn4_pkg.all;

entity warn4_dut is
    port (
        clk : in  std_logic;
        i   : in  in_t;
        o   : out std_logic_vector(1 downto 0) );
end entity;

architecture rtl of warn4_dut is
    signal x  : unsigned(2 downto 0);   -- Never driven: all 'U'
    signal r  : in_t;
    signal y0, y1 : std_logic;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            r <= i;
        end if;
    end process;

    p_in : process (i, x) is
    begin
        if i.k < x then
            y0 <= i.a;
        elsif i.v(3) = '1' then
            y0 <= i.b xor i.c xor i.d xor i.e xor i.f xor i.g;
        else
            y0 <= i.h xor i.i xor i.j;
        end if;
    end process;

    p_reg : process (r, x) is
    begin
        if r.k >= x then
            y1 <= r.a and r.j;
        else
            y1 <= r.v(0);
        end if;
    end process;

    o <= y0 & y1;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.warn4_pkg.all;

entity warn4_tb is
end entity;

architecture test of warn4_tb is
    signal clk : std_logic := '0';
    signal i   : in_t := ('0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
                          "000", (others => '0'));
    signal o   : std_logic_vector(1 downto 0);
begin
    dut : entity work.warn4_dut port map (clk, i, o);

    clk <= not clk after 5 ns when now < 150 ns;

    process is
    begin
        for n in 1 to 24 loop
            wait for 3 ns;
            case n mod 6 is
                when 0 => i.j <= not i.j;
                when 1 => i.e <= i.e;         -- No event
                when 2 => i.k <= i.k + 1;
                when 3 => i <= i;             -- No event on any element
                when 4 => i.v(n) <= '1';
                when others => i.a <= not i.a; i.f <= i.f;
            end case;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn4.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn4_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -gt 20

nvc --std=2008 --work=w_b -a warn4.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn4_tb \
    > b_dump.log
cat b_dump.log
grep -q 'WARN4_TB.DUT islands=1 fused_procs=4/4' b_dump.log
nvc --std=2008 --work=w_b -r warn4_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast warn4_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r warn4_tb > bna.log 2>&1

nvc --std=2008 --work=w_s -a warn4.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e warn4_tb
nvc --std=2008 --work=w_s -r warn4_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf bna s; do
    diff -u off.log $m.log
done
