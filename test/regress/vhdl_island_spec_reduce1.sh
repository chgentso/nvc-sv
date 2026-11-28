set -xe

# Speculative Mode A lowering of the VHDL-2008 reduction operators of
# STD_ULOGIC_VECTOR (STD_LOGIC_1164) and of UNSIGNED and SIGNED
# (NUMERIC_STD): the island gets a Mode A variant and runs it.  The output
# must be that of --rtl-islands=off.

cat >reduce1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity red_e is
    port ( clk : in  std_logic;
           a   : in  std_logic_vector(11 downto 0);
           b   : in  unsigned(9 downto 0);
           c   : in  signed(5 downto 0);
           q   : out std_logic_vector(11 downto 0);
           y   : out std_logic_vector(3 downto 0) );
end entity;
architecture rtl of red_e is
    signal r : std_logic_vector(11 downto 0) := (others => '0');
    signal t : std_logic_vector(3 downto 0);
begin
    regs : process (clk) is
    begin
        if rising_edge(clk) then
            r(0)  <= and a;
            r(1)  <= or a;
            r(2)  <= xor a;
            r(3)  <= nand a(3 downto 0);
            r(4)  <= nor (a(11 downto 6) and a(5 downto 0));
            r(5)  <= xnor a(7 downto 1);
            r(6)  <= or b;
            r(7)  <= and b(2 downto 0);
            r(8)  <= xor b;
            r(9)  <= nor c;
            r(10) <= nand c;
            r(11) <= xnor (c xor c(5 downto 0));
        end if;
    end process;
    comb : process (a, b, r) is
    begin
        t(0) <= or (a(1 downto 0));
        t(1) <= and r;
        t(2) <= xor (b(9 downto 4) & unsigned(r(3 downto 0)));
        t(3) <= nor r(11 downto 8);
    end process;
    q <= r;
    y <= t;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity reduce1_tb is end entity;
architecture test of reduce1_tb is
    signal clk : std_logic := '0';
    signal a : std_logic_vector(11 downto 0) := (others => '0');
    signal b : unsigned(9 downto 0) := (others => '0');
    signal c : signed(5 downto 0) := (others => '0');
    signal q : std_logic_vector(11 downto 0);
    signal y : std_logic_vector(3 downto 0);
begin
    u : entity work.red_e port map (clk, a, b, c, q, y);

    mon : process (q, y) is
    begin
        report to_string(q) & " " & to_string(y);
    end process;

    stim : process is
    begin
        for i in 0 to 199 loop
            a <= std_logic_vector(to_unsigned((i * 1637) mod 4096, 12));
            if i mod 17 = 5 then
                a <= (others => '1');
            end if;
            b <= to_unsigned((i * 97) mod 1024, 10);
            if i mod 13 = 7 then
                b(2 downto 0) <= "111";
            end if;
            c <= to_signed(i mod 64 - 32, 6);
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT
export NVC_ISLAND_SPEC_START=0      # Short run: promote from the start

nvc --std=2008 --work=w_off -a reduce1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e reduce1_tb -r 2>&1 \
    | grep -v '^\*\* Debug' > off.log

export NVC_ISLAND_SPEC=1
nvc --std=2008 --work=w_s -a reduce1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s --rtl-islands=hier \
    -e reduce1_tb > dump.log 2>&1
grep 'ISLAND_INFO' dump.log
grep -q 'REDUCE1_TB.U island=0 .* a_variant=yes' dump.log
nvc --std=2008 --work=w_s --rtl-islands=hier -e reduce1_tb
for st in 0 1 3; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r reduce1_tb 2>&1 \
        | grep -v '^\*\* Debug' > s${st}_raw.log
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log <(grep -v '^ISLAND_SPEC' s${st}_raw.log)
done
cat s0_raw.log | grep ISLAND_SPEC
grep -q '^ISLAND_SPEC_RT: island=0 mode=A .* scope=W_S.REDUCE1_TB.U$' s0_raw.log
grep -q '^ISLAND_SPEC_RT: deopts .* novar=0 ' s0_raw.log
