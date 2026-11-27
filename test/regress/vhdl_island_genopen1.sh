set -xe

# A generic that the instantiating generic map does not mention takes its
# default: the elaborated map holds an open actual for it, which an RTL
# island process that reads the generic (here through the generic of an
# inner instance) must replace with the default, as for an integer, a
# vector and a string generic.  The output must be identical to
# --rtl-islands=off.

cat >genopen1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity genopen_inner is
    generic (
        INIT  : std_logic_vector(7 downto 0);
        SHIFT : natural;
        NAME  : string );
    port (
        clk : in  std_logic;
        din : in  std_logic_vector(7 downto 0);
        o   : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of genopen_inner is
    signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
        end if;
    end process;

    p_comb : process (q) is
        variable v : std_logic_vector(7 downto 0);
    begin
        if q(0) = '1' then
            v := INIT;
        else
            v := std_logic_vector(shift_left(unsigned(q), SHIFT));
        end if;
        if q = x"ff" then
            report NAME & " saturated";
        end if;
        o <= v;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity genopen_mid is
    generic (
        INIT  : std_logic_vector(7 downto 0) := x"a5";
        SHIFT : natural := 3;
        NAME  : string := "mid" );
    port (
        clk : in  std_logic;
        din : in  std_logic_vector(7 downto 0);
        o   : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of genopen_mid is
begin
    u : entity work.genopen_inner
        generic map (INIT => INIT, SHIFT => SHIFT, NAME => NAME)
        port map (clk, din, o);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity genopen_tb is
end entity;

architecture test of genopen_tb is
    signal clk : std_logic := '0';
    signal din, o1, o2 : std_logic_vector(7 downto 0) := (others => '0');
begin
    -- No generic map at all, and only one of the three generics mapped
    m1 : entity work.genopen_mid port map (clk, din, o1);
    m2 : entity work.genopen_mid generic map (SHIFT => 1) port map (clk, din, o2);

    p_log : process (o1, o2) is
    begin
        report "o1=" & to_hstring(o1) & " o2=" & to_hstring(o2);
    end process;

    p_stim : process is
        variable v : unsigned(7 downto 0) := x"3c";
    begin
        for k in 0 to 9 loop
            din <= std_logic_vector(v);
            v := resize(v * 5, 8) + 17;
            if k = 6 then
                din <= x"ff";
            end if;
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a genopen1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e genopen_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log
grep -q "mid saturated" off.log

nvc --std=2008 --work=w_b -a genopen1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e genopen_tb \
    > b_dump.log
cat b_dump.log
grep -q 'M1.U islands=1 fused_procs=2/2' b_dump.log
grep -q 'M2.U islands=1 fused_procs=2/2' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd genopen_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast genopen_tb > bnf.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r genopen_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a genopen1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e genopen_tb
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r genopen_tb > s.log 2>&1

for m in b bnf bchk s; do
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
