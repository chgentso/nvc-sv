set -xe

# TO_STRING of a character literal (std_logic, bit, character) has no
# apostrophes, unlike 'IMAGE.  A report in a process fused into an RTL
# island must print the same text as with --rtl-islands=off, in Mode B
# and in Mode A.

cat >tostring1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tostring1 is
end entity;

architecture test of tostring1 is
    type state_t is (idle, busy);
    signal clk, d : std_logic := '0';
    signal q      : std_logic := '0';
    signal b      : bit := '0';
    signal st     : state_t := idle;
    signal n      : unsigned(3 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
            b <= '1' when d = '1' else '0';
            st <= busy when d = '1' else idle;
            n <= n + 1;
            report "q=" & to_string(q) & " b=" & to_string(b)
                & " st=" & to_string(st) & " n=" & to_string(to_integer(n))
                & " img=" & std_logic'image(q) & " bimg=" & bit'image(b)
                & " eq=" & to_string(q = '1');
        end if;
    end process;

    p_stim : process is
    begin
        for i in 0 to 5 loop
            d <= '1' when i mod 2 = 0 else '0';
            wait for 1 ns;
            clk <= '1';
            wait for 1 ns;
            clk <= '0';
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a tostring1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e tostring1 -r > off.log 2>&1
grep -q "q=1 b=1 st=busy n=1 img='1' bimg='1' eq=true" off.log

nvc --std=2008 --work=w_b -a tostring1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e tostring1 \
    > b_dump.log
grep -q 'TOSTRING1 islands=1 fused_procs=1/2' b_dump.log
nvc --std=2008 --work=w_b -r tostring1 > b.log 2>&1
diff -u off.log b.log

nvc --std=2008 --work=w_a -a tostring1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier \
    --2state=islands -e tostring1 > a_dump.log
grep -q 'TOSTRING1 islands=1 fused_procs=1/2' a_dump.log
nvc --std=2008 --work=w_a -r tostring1 > a.log 2>&1
diff -u off.log a.log
