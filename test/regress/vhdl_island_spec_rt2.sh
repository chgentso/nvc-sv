set -xe

# Speculative Mode A at run time: a deopt in the middle of a comb call.
# P1 writes X, then a '-' literal makes the Mode A function return a deopt
# code.  P2 reads X and ran before P1 (readers-first order): the island
# must run again in Mode B from the values X had before the call, or X and
# Z would change in the same delta cycle.  The monitor reports every delta
# in which X, Y or Z change.  From k = 60 the '-' branch is taken on every
# change (repeated deopts back off).

cat >rt2.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

-- P1 writes X and then takes a '-' branch: the Mode A call stops after X
-- was written.  P2 reads X and runs first (readers-first order), so it must
-- see the old X when the island runs again in Mode B.
entity chain_e is
    port ( a   : in  std_logic_vector(1 downto 0);
           sel : in  std_logic_vector(1 downto 0);
           x_o, y_o, z_o : out std_logic_vector(1 downto 0) );
end entity;
architecture rtl of chain_e is
    signal x, y, z : std_logic_vector(1 downto 0) := "00";
begin
    p1 : process (a, sel) is
    begin
        x <= a;
        if sel = "11" then
            y <= "1-";
        else
            y <= a;
        end if;
    end process;
    p2 : process (x) is
    begin
        z <= not x;
    end process;
    x_o <= x;
    y_o <= y;
    z_o <= z;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity rt2_tb is end entity;

architecture test of rt2_tb is
    signal a, sel, x, y, z : std_logic_vector(1 downto 0) := "00";
begin
    u : entity work.chain_e port map (a, sel, x, y, z);

    -- Reports every delta cycle in which X, Y or Z change
    mon : process (x, y, z) is
    begin
        report to_string(x) & " " & to_string(y) & " " & to_string(z);
    end process;

    stim : process is
    begin
        for k in 0 to 99 loop
            a <= (a(0), not a(1));
            if k mod 7 = 3 or k >= 60 then
                sel <= "11";
            else
                sel <= "01";
            end if;
            wait for 10 ns;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a rt2.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e rt2_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
nvc --std=2008 --work=w_b -a rt2.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e rt2_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > b.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log b.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a rt2.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e rt2_tb
for st in 0 1 3; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=1 \
        nvc --std=2008 --work=w_s -r rt2_tb 2>&1 \
        | grep -v '^\*\* Debug' > s${st}_raw.log
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log <(grep -v '^ISLAND_SPEC' s${st}_raw.log)
done
cat s0_raw.log | grep ISLAND_SPEC
grep -q '^ISLAND_SPEC_RT: deopts literal=[1-9]' s0_raw.log
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=1 promoted=1 ' s0_raw.log
