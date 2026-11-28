set -xe

# A slice of a SIGNED value and SIGNED()/UNSIGNED() conversions are signed
# or unsigned as their VHDL type says in the speculative 2-state variant
# ($a): widening for a product, RESIZE, comparisons, shifts and TO_INTEGER
# used to zero-extend or compare unsigned.  Output must be the same as
# --rtl-islands=off with NVC_ISLAND_SPEC=1, also with every eligible
# island in 2-state from time zero (NVC_ISLAND_SPEC_FORCE=A).

cat >sign1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sg_e is
    port ( clk : in std_logic; a : in signed(15 downto 0);
           u : in std_logic_vector(7 downto 0);
           p : out signed(19 downto 0); r : out signed(11 downto 0);
           c : out std_logic_vector(3 downto 0); sh : out signed(7 downto 0);
           n : out integer range -128 to 127; m : out signed(15 downto 0) );
end entity;
architecture rtl of sg_e is
    signal acc : signed(23 downto 0) := x"a5c3e1";
begin
    p  <= a(13 downto 4) * acc(20 downto 11);
    r  <= resize(a(15 downto 8), 12);
    c(0) <= '1' when a(11 downto 4) < acc(7 downto 0) else '0';
    c(1) <= '1' when a(11 downto 4) >= signed(u) else '0';
    c(2) <= '1' when unsigned(a(11 downto 4)) > unsigned(acc(7 downto 0)) else '0';
    c(3) <= '1' when maximum(a(7 downto 0), acc(15 downto 8)) = acc(15 downto 8) else '0';
    sh <= shift_right(a(15 downto 8), 3);
    n  <= to_integer(a(9 downto 2));
    m  <= signed(u) * a(15 downto 8);
    process (clk) is
    begin
        if rising_edge(clk) then
            acc <= (acc(22 downto 11) * a(15 downto 4)) xor x"5a5a5a";
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity sign1_tb is end entity;
architecture test of sign1_tb is
    signal clk : std_logic := '0';
    signal a : signed(15 downto 0) := x"0000";
    signal u : std_logic_vector(7 downto 0) := x"00";
    signal p : signed(19 downto 0);
    signal r : signed(11 downto 0);
    signal c : std_logic_vector(3 downto 0);
    signal sh : signed(7 downto 0);
    signal n : integer range -128 to 127;
    signal m : signed(15 downto 0);
begin
    uu : entity work.sg_e port map (clk, a, u, p, r, c, sh, n, m);
    process is
    begin
        for k in 0 to 23 loop
            a <= to_signed((k * 7919 + 1237) mod 65536 - 32768, 16);
            u <= std_logic_vector(to_unsigned((k * 101 + 77) mod 256, 8));
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
            report integer'image(k) & " p=" & to_hstring(p) & " r="
                & to_hstring(r) & " c=" & to_string(c) & " sh="
                & to_hstring(sh) & " n=" & integer'image(n) & " m="
                & to_hstring(m);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a sign1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e sign1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -a sign1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -e sign1_tb
nvc --std=2008 --work=w_s -r sign1_tb 2>&1 | grep -v '^\*\* Debug' > s.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r sign1_tb 2>&1 \
    | grep -v '^\*\* Debug' > a.log
diff -u off.log s.log
diff -u off.log a.log
grep -c 'p=' off.log | grep -qx 24
