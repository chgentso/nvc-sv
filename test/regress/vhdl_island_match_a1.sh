set -xe

# IS_X, STD_MATCH and the matching operators in Mode A (--2state=islands),
# where only '0' and '1' exist: IS_X is false, matching is equality.  The
# output must equal the Mode B and --rtl-islands=off runs for 0/1 stimuli.

cat >match_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity match_dut is
    port (
        clk  : in  std_logic;
        d    : in  std_logic_vector(7 downto 0);
        flag : out std_logic;
        flg1 : out std_logic;
        eq1  : out std_logic;
        eqv  : out std_logic;
        nev  : out std_logic;
        lt1  : out std_logic;
        ge1  : out std_logic;
        sm1  : out std_logic;
        smv  : out std_logic;
        smu  : out std_logic;
        bq   : out bit;
        q    : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of match_dut is
    signal r : std_logic_vector(7 downto 0) := (others => '0');
    signal b1, b2 : bit := '0';
begin
    p_reg : process (clk) is
    begin
        if rising_edge(clk) then
            r  <= d;
            if d(0) = '1' then b1 <= '1'; else b1 <= '0'; end if;
            if d(1) = '1' then b2 <= '1'; else b2 <= '0'; end if;
        end if;
    end process;
    q <= r;

    p_isx : process (r) is
    begin
        if is_x(r) then flag <= '1'; else flag <= '0'; end if;
        if is_x(r(3)) then flg1 <= '1'; else flg1 <= '0'; end if;
    end process;

    p_eq : process (r) is
    begin
        eq1 <= r(0) ?= r(1);
        eqv <= r ?= x"5a";
        nev <= r ?/= x"a5";
    end process;

    p_ord : process (r) is
    begin
        lt1 <= r(2) ?< r(3);
        ge1 <= r(4) ?>= r(5);
    end process;

    p_sm : process (r) is
    begin
        if std_match(r(1), r(2)) then sm1 <= '1'; else sm1 <= '0'; end if;
        if std_match(r, std_logic_vector'(x"3c")) then smv <= '1'; else smv <= '0'; end if;
        if std_match(unsigned(r), unsigned'(x"c3")) then smu <= '1'; else smu <= '0'; end if;
    end process;

    p_bit : process (b1, b2) is
    begin
        bq <= b1 ?= b2;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity match_tb is
end entity;

architecture test of match_tb is
    signal clk : std_logic := '0';
    signal d, q : std_logic_vector(7 downto 0) := (others => '0');
    signal flag, flg1, eq1, eqv, nev, lt1, ge1, sm1, smv, smu : std_logic;
    signal bq : bit;
begin
    dut : entity work.match_dut
        port map (clk, d, flag, flg1, eq1, eqv, nev, lt1, ge1, sm1, smv, smu,
                  bq, q);

    stim : process is
        variable v : unsigned(7 downto 0) := x"01";
    begin
        for k in 0 to 23 loop
            case k is
                when 5  => v := x"5a";
                when 9  => v := x"3c";
                when 13 => v := x"c3";
                when 17 => v := x"a5";
                when others => v := v + x"2b";
            end case;
            d <= std_logic_vector(v);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            report integer'image(k) & " " & to_hstring(q) & " "
               & std_logic'image(flag) & std_logic'image(flg1)
               & std_logic'image(eq1) & std_logic'image(eqv)
               & std_logic'image(nev) & std_logic'image(lt1)
               & std_logic'image(ge1) & std_logic'image(sm1)
               & std_logic'image(smv) & std_logic'image(smu)
               & bit'image(bq);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a match_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e match_tb -r 2>&1 | sed '/JIT code footprint/d' | sort > off.log

nvc --std=2008 --work=w_b -a match_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e match_tb >b_dump.log
nvc --std=2008 --work=w_b -r match_tb 2>&1 | sed '/JIT code footprint/d' | sort > b.log

nvc --std=2008 --work=w_a -a match_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e match_tb >a_dump.log
nvc --std=2008 --work=w_a -r match_tb 2>&1 | sed '/JIT code footprint/d' | sort > a.log

cat a_dump.log b_dump.log
grep -q "'1''1'" off.log
grep -q "'0''0'" off.log
diff -u off.log b.log
diff -u off.log a.log

# Mode A fuses every process of the DUT; Mode B keeps the ones it lowers
grep -q 'MATCH_TB.DUT islands=[0-9]* fused_procs=7/7' a_dump.log
! grep -q 'VHDL_ISLAND_REJECT: .*MATCH_TB.DUT' a_dump.log
