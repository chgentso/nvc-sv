set -xe

# Speculative Mode A functions are lowered the first time the runtime calls
# them.  nv_e has a Mode A variant whose lowering fails (a reduction "or"
# of a slice expression): its first Mode A call returns the "novar" deopt
# and pins the island to Mode B; ok_e is promoted and runs Mode A.  Output
# and waveforms must be those of --rtl-islands=off, also with
# NVC_ISLAND_SPEC_STRESS (conversions on every delta).

cat >lazy.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity nv_e is
    port ( clk : in std_logic;
           a, b : in std_logic_vector(40 downto 0);
           q : out std_logic );
end entity;
architecture rtl of nv_e is
    signal r : std_logic := '0';
begin
    process (clk) is
        variable nz : std_logic;
    begin
        if rising_edge(clk) then
            nz := or(a(36 downto 2) and not b(34 downto 0));
            r <= nz;
        end if;
    end process;
    q <= r;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ok_e is
    port ( clk : in std_logic; d : in std_logic_vector(7 downto 0);
           q : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of ok_e is
    signal r : std_logic_vector(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= std_logic_vector(unsigned(r) + unsigned(d));
        end if;
    end process;
    q <= r xor d;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity lazy_tb is end entity;
architecture test of lazy_tb is
    signal clk : std_logic := '0';
    signal a, b : std_logic_vector(40 downto 0) := (others => '0');
    signal d, mon_q : std_logic_vector(7 downto 0) := x"00";
    signal mon_nz : std_logic;
begin
    u_nv: entity work.nv_e port map (clk, a, b, mon_nz);
    u_ok: entity work.ok_e port map (clk, d, mon_q);

    stim: process is
    begin
        for i in 0 to 39 loop
            a <= std_logic_vector(to_unsigned(i * 977, 41) sll (i mod 7));
            b <= std_logic_vector(to_unsigned(i * 31337, 41) rol i);
            d <= std_logic_vector(to_unsigned(i * 13, 8));
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            report integer'image(i) & " nz=" & std_logic'image(mon_nz)
                & " q=" & to_string(mon_q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT NVC_DUMP_ISLANDS

nd() { sed '/^\$date/,/\$end/d' $1; }

nvc --std=2008 --work=w_off -a lazy.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e lazy_tb \
    -r --wave=off.vcd --format=vcd 2>&1 | grep -v '^\*\* Debug' > off.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a lazy.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e lazy_tb
for st in 0 1 3; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r --wave=s$st.vcd --format=vcd \
        lazy_tb 2>&1 | grep -v '^\*\* Debug' > s${st}_raw.log
    grep -v '^ISLAND_SPEC' s${st}_raw.log > s$st.log
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log s$st.log
    diff -u <(nd off.vcd) <(nd s$st.vcd)
done

cat s0_raw.log | grep ISLAND_SPEC
grep -q ' 39 nz=.1. q=' off.log
# Both islands have a variant; nv_e was promoted once and pinned by its
# failed lowering, ok_e ends in Mode A
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=2 promoted=2 in_a=1 pinned=1 ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: island=[0-9]* mode=B promotions=1 .* pinned=novar scope=.*U_NV$' s0_raw.log
grep -q '^ISLAND_SPEC_RT: pinned .* novar=1' s0_raw.log
grep -q '^ISLAND_SPEC_RT: pinned .* novar=1' s3_raw.log

# NVC_DUMP_ISLANDS lowers the Mode A functions at once to give the reason
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s --rtl-islands=hier \
    -e lazy_tb > dump.log 2>&1
grep -q 'U_NV island=0 .* a_variant=no reason=lower expr T_FCALL "or"' dump.log
grep -q 'U_OK island=0 .* a_variant=yes' dump.log
