set -xe

# A combinational process that may warn and is sensitive to a signal that
# only a branch that is never taken reads (D below) must run on every event
# on that signal.  Its speculative Mode A variant ran it only when a signal
# it loads changed, so a change of D while the island was in Mode A did not
# reach the metavalue (U) that makes the process warn: one warning less
# than with --rtl-islands=off.  NVC_ISLAND_SPEC_STRESS promotes the island
# on every delta despite the held signal.

cat >swarn2.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity swarn2_tb is
end entity;

architecture test of swarn2_tb is
    signal clk : std_logic := '0';
    signal d   : unsigned(2 downto 0) := "000";
    signal q   : unsigned(2 downto 0) := "000";
    signal u   : unsigned(2 downto 0);   -- Never driven: all 'U'
    signal y   : std_logic;
begin
    clk <= not clk after 5 ns when now < 50 ns;

    stim : process is
    begin
        for i in 1 to 6 loop
            wait for 7 ns;
            d <= d + 1;
        end loop;
        wait;
    end process;

    process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
        end if;
    end process;

    process (d, q, u) is
    begin
        if q > u then
            y <= '1';
        elsif unsigned'("01") > unsigned'("10") then
            y <= d(0);
        else
            y <= '0';
        end if;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a swarn2.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e swarn2_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
test $(grep -c 'metavalue detected' off.log) -ge 10

NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -a swarn2.vhd
NVC_DUMP_ISLANDS=1 NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s \
    --rtl-islands=hier -e swarn2_tb > dump.log
cat dump.log
grep -q 'a_variant=yes' dump.log
nvc --std=2008 --work=w_s -r swarn2_tb 2>&1 | grep -v '^\*\* Debug' > s.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r swarn2_tb 2>&1 \
    | grep -v '^\*\* Debug' > a.log
for n in 1 2 3; do
    NVC_ISLAND_SPEC_STRESS=$n nvc --std=2008 --work=w_s -r swarn2_tb 2>&1 \
        | grep -v '^\*\* Debug' > st$n.log
done
for m in s a st1 st2 st3; do
    diff -u off.log $m.log
done
