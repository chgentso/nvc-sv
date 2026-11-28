set -xe

# The run guard of a combinational process that may warn compares the
# signals of its sensitivity list with copies taken on the previous call,
# using the change bytes of the island to skip signals that did not change:
# also signals that only a flip-flop or a branch that is never taken reads,
# long sensitivity lists of record elements and signals longer than a few
# words.  The output must be identical to --rtl-islands=off.

cat >warn3.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package warn3_pkg is
    type rec_t is record
        f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11 : std_logic;
        n0, n1 : unsigned(1 downto 0);
        w : unsigned(39 downto 0);
    end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.warn3_pkg.all;

entity warn3_dut is
    port (
        clk : in  std_logic;
        d   : in  unsigned(3 downto 0);
        o   : out std_logic_vector(2 downto 0) );
end entity;

architecture rtl of warn3_dut is
    signal q, cnt : unsigned(3 downto 0) := "0000";
    signal x  : unsigned(3 downto 0);    -- Never driven: all 'U'
    signal xw : unsigned(39 downto 0);   -- Likewise
    signal r  : rec_t := ('0', '0', '0', '0', '0', '0', '0', '0', '0', '0',
                          '0', '0', "00", "00", (others => '0'));
    signal y0, y1, y2 : std_logic;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
            cnt <= cnt + 1;
            r.f0 <= cnt(0);
            r.f5 <= cnt(1);
            r.f9 <= cnt(2);
            r.n1 <= cnt(3 downto 2);
            if cnt(1 downto 0) = "11" then
                r.w <= r.w + 1;
            end if;
            -- Assigned, never change
            r.f1 <= '0'; r.f2 <= '0'; r.f3 <= '0'; r.f4 <= '0';
            r.f6 <= '0'; r.f7 <= '0'; r.f8 <= '0'; r.f10 <= '0';
            r.f11 <= '0'; r.n0 <= "00";
        end if;
    end process;

    -- Sensitive to d, which only the flip-flop and a branch that is never
    -- taken read
    p_d : process (d, q, x) is
    begin
        if q < x then
            y0 <= '1';
        elsif unsigned'("0011") >= unsigned'("1111") then
            y0 <= d(0);
        else
            y0 <= '0';
        end if;
    end process;

    -- Sensitive to every element of a record and a long vector
    p_rec : process (r, x, xw) is
    begin
        if r.w < xw then
            y1 <= r.f0;
        elsif r.n1 = x(1 downto 0) then
            y1 <= r.f5 xor r.f9;
        else
            y1 <= r.f11;
        end if;
    end process;

    -- Sensitive to elements of the record that do not change
    p_rec2 : process (r.f1, r.f2, r.f3, r.f4, r.f6, r.f7, r.f8, r.f10,
                      r.n0, r.f11, r.f9, x) is
    begin
        if r.n0 > x(1 downto 0) then
            y2 <= r.f1;
        else
            y2 <= r.f9;
        end if;
    end process;

    o <= y0 & y1 & y2;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity warn3_tb is
end entity;

architecture test of warn3_tb is
    signal clk : std_logic := '0';
    signal d   : unsigned(3 downto 0) := "0000";
    signal o   : std_logic_vector(2 downto 0);
begin
    dut : entity work.warn3_dut port map (clk, d, o);

    clk <= not clk after 5 ns when now < 200 ns;

    process is
    begin
        for i in 1 to 20 loop
            wait for 7 ns;
            if i mod 5 = 0 then
                d <= d;   -- A transaction without an event
            else
                d <= d + 1;
            end if;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a warn3.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e warn3_tb -r > off.log 2>&1
test $(grep -c 'metavalue detected' off.log) -gt 50

nvc --std=2008 --work=w_b -a warn3.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e warn3_tb \
    > b_dump.log
cat b_dump.log
grep -q 'WARN3_TB.DUT islands=1 fused_procs=5/5' b_dump.log
nvc --std=2008 --work=w_b -r warn3_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast warn3_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r warn3_tb > bna.log 2>&1

nvc --std=2008 --work=w_s -a warn3.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e warn3_tb
nvc --std=2008 --work=w_s -r warn3_tb > s.log 2>&1

sed -i '/^\*\* Debug: /d' *.log   # debug builds only
for m in b bnf bna s; do
    diff -u off.log $m.log
done
