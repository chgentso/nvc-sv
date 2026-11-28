set -xe

# Speculative two-state islands are on by default.  --no-island-spec when
# elaborating, or --no-island-spec / NVC_ISLAND_SPEC=0 when running, turns
# them off; --island-spec when elaborating turns them on again after
# NVC_ISLAND_SPEC=0.  The output is the same in every case.

cat >sdef1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity cnt_e is
    port ( clk : in std_logic; d : in unsigned(7 downto 0);
           q : out unsigned(7 downto 0) );
end entity;
architecture rtl of cnt_e is
    signal r : unsigned(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            r <= r + d;
        end if;
    end process;
    q <= r xor d;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sdef1_tb is end entity;
architecture test of sdef1_tb is
    signal clk : std_logic := '0';
    signal d, q : unsigned(7 downto 0) := x"00";
begin
    u : entity work.cnt_e port map (clk, d, q);

    stim : process is
    begin
        for i in 0 to 49 loop
            d <= to_unsigned(i * 7, 8);
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
            report integer'image(i) & " " & to_hstring(q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT
export NVC_ISLAND_SPEC_START=0      # Short run: promote from the start

run() {
    NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 "$@" 2>&1 \
        | grep -v '^\*\* Debug' > $log
    sed -i '/^\*\* Debug: /d' $log   # debug builds only: JIT code footprint etc.
}

nvc --std=2008 --work=w_off -a sdef1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e sdef1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
sed -i '/^\*\* Debug: /d' off.log

# Default: on
nvc --std=2008 --work=w_d -a sdef1.vhd
nvc --std=2008 --work=w_d --rtl-islands=hier -e sdef1_tb
log=d.log run --work=w_d -r sdef1_tb
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=1 promoted=1 ' d.log
diff -u off.log <(grep -v '^ISLAND_SPEC' d.log)

# Turned off when running
log=dr.log run --work=w_d -r --no-island-spec sdef1_tb
NVC_ISLAND_SPEC=0 log=de.log run --work=w_d -r sdef1_tb
for f in dr de; do
    grep -q '^ISLAND_SPEC: islands=1 a_variant=0$' $f.log
    if grep -q '^ISLAND_SPEC_RT' $f.log; then exit 1; fi
    diff -u off.log <(grep -v '^ISLAND_SPEC' $f.log)
done

# Turned off when elaborating: the run cannot turn it on
nvc --std=2008 --work=w_n -a sdef1.vhd
nvc --std=2008 --work=w_n --rtl-islands=hier -e --no-island-spec sdef1_tb
log=n.log run --work=w_n -r sdef1_tb
grep -q '^ISLAND_SPEC: islands=1 a_variant=0$' n.log
if grep -q '^ISLAND_SPEC_RT' n.log; then exit 1; fi
diff -u off.log <(grep -v '^ISLAND_SPEC' n.log)

# --island-spec overrides NVC_ISLAND_SPEC=0 when elaborating
export NVC_ISLAND_SPEC=0
nvc --std=2008 --work=w_e -a sdef1.vhd
nvc --std=2008 --work=w_e --rtl-islands=hier -e --island-spec sdef1_tb
unset NVC_ISLAND_SPEC
log=e.log run --work=w_e -r sdef1_tb
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=1 promoted=1 ' e.log
diff -u off.log <(grep -v '^ISLAND_SPEC' e.log)
