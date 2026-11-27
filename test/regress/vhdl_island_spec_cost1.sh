set -xe

# Cost of speculative Mode A promotion.  The registers of rs_e are 'U'
# until its first clock edge in reset: they clear within the wait, so it is
# promoted once, holding nothing.  cq_e reads a register that is never written every
# other cycle: each promotion holding it deopts a few ticks later, and
# after a few such promotions it is retried only rarely ("pinned by
# cost").  Output and waveforms must be those of --rtl-islands=off, also
# with NVC_ISLAND_SPEC_STRESS (conversions on every delta).

cat >cost.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rs_e is
    port ( clk, rst : in std_logic; d : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of rs_e is
    signal r1, r2 : std_logic_vector(7 downto 0);
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                r1 <= x"00";
                r2 <= x"00";
            else
                r1 <= std_logic_vector(unsigned(r1) + unsigned(d));
                r2 <= r1 xor d;
            end if;
        end if;
    end process;
    q <= r2;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cq_e is
    port ( clk, sel : in std_logic; d : in std_logic_vector(7 downto 0);
           y : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of cq_e is
    signal ru : std_logic_vector(7 downto 0);   -- Never written
    signal r : std_logic_vector(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            if sel = '1' then
                r <= ru xor d;
            else
                r <= d;
            end if;
        end if;
    end process;
    y <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cost_tb is end entity;
architecture test of cost_tb is
    signal clk, sel : std_logic := '0';
    signal rst : std_logic := '1';
    signal d, mon_q, mon_y : std_logic_vector(7 downto 0) := x"00";
begin
    u_rs: entity work.rs_e port map (clk, rst, d, mon_q);
    u_cq: entity work.cq_e port map (clk, sel, d, mon_y);

    stim: process is
    begin
        for i in 0 to 1999 loop
            if i = 6 then
                rst <= '0';
            end if;
            d <= std_logic_vector(to_unsigned((i * 37) mod 256, 8));
            sel <= '1' when i mod 2 = 0 else '0';
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            if i mod 100 = 0 or i > 1990 then
                report integer'image(i) & " q=" & to_string(mon_q)
                    & " y=" & to_string(mon_y);
            end if;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT NVC_ISLAND_SPEC_HOLD_WAIT

nd() { sed '/^\$date/,/\$end/d' $1; }

nvc --std=2008 --work=w_off -a cost.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e cost_tb \
    -r --wave=off.vcd --format=vcd 2>&1 | grep -v '^\*\* Debug' > off.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a cost.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e cost_tb
for st in 0 1 3; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r --wave=s$st.vcd --format=vcd \
        cost_tb 2>&1 | grep -v '^\*\* Debug' > s${st}_raw.log
    grep -v '^ISLAND_SPEC' s${st}_raw.log > s$st.log
    diff -u off.log s$st.log
    diff -u <(nd off.vcd) <(nd s$st.vcd)
done

cat s0_raw.log | grep ISLAND_SPEC
grep -q ' 0 q=00000000 y=UUUUUUUU' off.log
grep -q ' 1998 q=[01]* y=UUUUUUUU' off.log
grep -q ' 1999 q=[01]* y=11101011' off.log
# rs_e waited for its registers to clear and was promoted once, holding
# nothing; cq_e is pinned by cost after a few short promotions
grep -q '^ISLAND_SPEC_RT: island=[0-9]* mode=A promotions=1 deopts=0 unclean=0 row=0 held=0 .* scope=.*U_RS$' s0_raw.log
grep -q '^ISLAND_SPEC_RT: island=[0-9]* mode=B promotions=[4-6] deopts=0 unclean=[4-6] .* scope=.*U_CQ$' s0_raw.log
grep -q '^ISLAND_SPEC_RT: cost pinned_by_cost=1 cost_pins=1 hold_waits=[1-9]' s0_raw.log
