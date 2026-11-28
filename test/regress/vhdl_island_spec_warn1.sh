set -xe

# A combinational process that may warn runs in Mode B only when a signal
# of its sensitivity list changed.  Its speculative Mode A variant has no
# such guard and cleared the run flag before the body, so when the body
# loaded a held metavalue (U below) and the call deopted, the Mode B call
# that replaces it ran the process unconditionally on an event that only
# touched bits it is not sensitive to (A(0)), and warned once more than
# with --rtl-islands=off.  When Mode B decides with its guard instead, the
# deopt must undo what the process assigned before it.  NVC_ISLAND_SPEC_STRESS promotes the island on
# every delta despite the held signal.

cat >swarn1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity swarn1_tb is
end entity;

architecture test of swarn1_tb is
    signal clk : std_logic := '0';
    signal a   : unsigned(5 downto 0) := "000110";
    signal u   : unsigned(2 downto 0);   -- Never driven: all 'U'
    signal b   : unsigned(5 downto 0);
    signal n   : unsigned(2 downto 0) := "000";
    signal y   : unsigned(3 downto 0);
begin
    clk <= not clk after 4 ns when now < 60 ns;

    process (clk) is
    begin
        if rising_edge(clk) then
            a(0) <= not a(0);
            n <= n + 1;
            if n = 5 then
                a(5) <= '1';
            end if;
        end if;
    end process;

    -- Sensitive to A(5 downto 3) only
    b <= "000000" when a(5 downto 3) = u else a(5 downto 3) & "111";

    -- Likewise, and assigns Y before the deopt: the deopt must undo that
    -- when Mode B does not run the process
    p_def : process (all) is
    begin
        y <= "0000";
        if a(5 downto 3) = u then
            y <= "1111";
        else
            y <= '1' & a(5 downto 3);
        end if;
    end process;

    mon : process (y) is
    begin
        report "y=" & to_hstring(y);
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a swarn1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e swarn1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
test $(grep -c 'metavalue detected' off.log) -eq 4
grep -q '44ns+2: y=C' off.log

NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -a swarn1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e swarn1_tb
nvc --std=2008 --work=w_s -r swarn1_tb 2>&1 | grep -v '^\*\* Debug' > s.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r swarn1_tb 2>&1 \
    | grep -v '^\*\* Debug' > a.log
for n in 1 2 3; do
    NVC_ISLAND_SPEC_STRESS=$n nvc --std=2008 --work=w_s -r swarn1_tb 2>&1 \
        | grep -v '^\*\* Debug' > st$n.log
done
for m in s a st1 st2 st3; do
    diff -u off.log $m.log
done
