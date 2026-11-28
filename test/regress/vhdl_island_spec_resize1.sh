set -xe

# RESIZE of a SIGNED value to one bit keeps the sign bit (numeric_std); the
# speculative 2-state variant ($a) returned the low bit.  Output must be the
# same as --rtl-islands=off with NVC_ISLAND_SPEC=1, also with every
# eligible island in 2-state from time zero (NVC_ISLAND_SPEC_FORCE=A).

cat >resize1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rs_e is
    port ( clk : in std_logic; a : in signed(7 downto 0);
           s1 : out signed(0 downto 0); s3 : out signed(2 downto 0);
           u1 : out unsigned(0 downto 0); r1 : out std_logic_vector(0 downto 0) );
end entity;
architecture rtl of rs_e is
    signal w : signed(19 downto 0);
    signal q : signed(0 downto 0) := "0";
begin
    w  <= resize(a, 20) xor signed'(x"5a3c1");
    s1 <= resize(w, 1);
    s3 <= resize(w, 3);
    u1 <= resize(unsigned(w), 1);
    process (clk) is
    begin
        if rising_edge(clk) then
            q <= resize(a, 1);
        end if;
    end process;
    r1 <= std_logic_vector(q);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity resize1_tb is end entity;
architecture test of resize1_tb is
    signal clk : std_logic := '0';
    signal a : signed(7 downto 0) := x"00";
    signal s1 : signed(0 downto 0);
    signal s3 : signed(2 downto 0);
    signal u1 : unsigned(0 downto 0);
    signal r1 : std_logic_vector(0 downto 0);
begin
    u : entity work.rs_e port map (clk, a, s1, s3, u1, r1);
    process is
    begin
        for k in 0 to 23 loop
            a <= to_signed((k * 37 + 3) mod 256 - 128, 8);
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
            report integer'image(k) & " a=" & to_string(a) & " s1="
                & to_string(s1) & " s3=" & to_string(s3) & " u1="
                & to_string(u1) & " r1=" & to_string(r1);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nvc --std=2008 --work=w_off -a resize1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e resize1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -a resize1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s -e resize1_tb
nvc --std=2008 --work=w_s -r resize1_tb 2>&1 | grep -v '^\*\* Debug' > s.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r resize1_tb 2>&1 \
    | grep -v '^\*\* Debug' > a.log
diff -u off.log s.log
diff -u off.log a.log
# A negative value keeps its sign bit
grep -q ' 0 a=10000011 s1=1 s3=110 ' off.log
