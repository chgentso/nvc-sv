set -xe

# 2D array variables and whole-array copies in Mode A (--2state=islands):
# results must equal the Mode B and --rtl-islands=off runs.

cat >arr2d_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity arr2d_dut is
    port (
        clk   : in  std_logic;
        sel   : in  unsigned(1 downto 0);
        sel2  : in  unsigned(1 downto 0);
        din   : in  std_logic_vector(7 downto 0);
        dout  : out std_logic_vector(7 downto 0);
        mid   : out std_logic_vector(4 downto 0);
        bitv  : out std_logic;
        cpy   : out std_logic_vector(7 downto 0);
        rq    : out std_logic_vector(7 downto 0);
        bigo  : out std_logic_vector(15 downto 0);
        bigr  : out std_logic_vector(15 downto 0);
        wo    : out std_logic_vector(71 downto 0) );
end entity;

architecture rtl of arr2d_dut is
    type mem_t is array (0 to 3) of std_logic_vector(7 downto 0);
    type smem_t is array (0 to 2) of signed(5 downto 0);
    type wide_t is array (0 to 3) of std_logic_vector(71 downto 0);
    signal wsig    : wide_t := (others => (others => '0'));
    type big_t is array (0 to 15) of std_logic_vector(15 downto 0);
    signal bigs    : big_t := (others => (others => '0'));
    signal sarr    : mem_t := (others => (others => '0'));
    signal sarr2   : mem_t := (others => (others => '0'));
    signal q       : std_logic_vector(7 downto 0) := (others => '0');
    signal dsel    : std_logic_vector(7 downto 0) := (others => '0');
    signal sg      : smem_t := (others => (others => '0'));
    signal sgo     : signed(5 downto 0) := (others => '0');
    signal sgs     : signed(5 downto 0) := (others => '0');
begin
    p_comb : process (sel, sel2, din, sarr2) is
        variable m : mem_t;
        variable n : mem_t;
    begin
        m := (others => din);
        m(to_integer(sel)) := din xor x"ff";
        m(0)(0) := '1';
        m(1)(3 downto 2) := "10";
        m(3)(to_integer(sel2)) := '0';
        dsel <= m(to_integer(sel2));
        mid  <= m(2)(5 downto 1);
        bitv <= m(to_integer(sel))(4);
        sarr <= m;
        n := sarr2;
        cpy <= n(to_integer(sel)) or n(3);
    end process;

    p_big : process (sel, sel2, din) is
        variable b : big_t;
    begin
        b := (others => din & din);
        b(to_integer(sel & sel2)) := x"1234" xor (din & din);
        b(5)(3 downto 0) := std_logic_vector(sel & sel2);
        b(to_integer(sel2 & sel))(15) := '0';
        bigo <= b(to_integer(sel2 & sel));
        bigs <= b;
    end process;

    bigr <= bigs(3) xor bigs(12);

    p_wide : process (sel, sel2, din) is
        variable w : wide_t;
        variable x : std_logic_vector(71 downto 0);
    begin
        x := din & din & din & din & din & din & din & din & din;
        w := (others => x);
        w(to_integer(sel)) := not x;
        wsig <= w;
    end process;

    wo <= wsig(to_integer(sel2)) xor wsig(1);

    p_sg : process (sel, din) is
        variable v : smem_t;
    begin
        v := (others => (others => '0'));
        v(0) := signed(din(5 downto 0));
        v(1) := signed(din(7 downto 2));
        v(2) := v(0) + v(1);
        sg <= v;
        sgo <= v(to_integer(sel(1 downto 1)));
    end process;

    p_reg : process (clk) is
    begin
        if rising_edge(clk) then
            q     <= dsel;
            sarr2 <= sarr;
            sgs   <= sgo;
        end if;
    end process;

    dout <= q;
    rq   <= std_logic_vector(resize(unsigned(sgs), 8)) xor sarr2(2);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity arr2d_tb is
end entity;

architecture test of arr2d_tb is
    signal clk  : std_logic := '0';
    signal sel, sel2 : unsigned(1 downto 0) := "00";
    signal din, dout, cpy, rq : std_logic_vector(7 downto 0) := (others => '0');
    signal bigo, bigr : std_logic_vector(15 downto 0);
    signal wo : std_logic_vector(71 downto 0);
    signal mid  : std_logic_vector(4 downto 0);
    signal bitv : std_logic;
begin
    dut : entity work.arr2d_dut
        port map (clk, sel, sel2, din, dout, mid, bitv, cpy, rq, bigo, bigr, wo);

    stim : process is
        variable d : unsigned(7 downto 0) := x"a5";
    begin
        for i in 0 to 15 loop
            sel  <= to_unsigned(i mod 4, 2);
            sel2 <= to_unsigned((i * 3 + 1) mod 4, 2);
            d := d + x"3d";
            din <= std_logic_vector(d);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            report integer'image(i) & " " & to_hstring(dout) & " "
               & to_string(mid) & " " & std_logic'image(bitv) & " "
               & to_hstring(cpy) & " " & to_hstring(rq) & " "
               & to_hstring(bigo) & " " & to_hstring(bigr) & " " & to_hstring(wo);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a arr2d_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e arr2d_tb -r 2>&1 | sort > off.log

nvc --std=2008 --work=w_b -a arr2d_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e arr2d_tb >b_dump.log
nvc --std=2008 --work=w_b -r arr2d_tb 2>&1 | sort > b.log

nvc --std=2008 --work=w_a -a arr2d_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e arr2d_tb >a_dump.log
nvc --std=2008 --work=w_a -r arr2d_tb 2>&1 | sort > a.log

cat a_dump.log
test $(wc -l < off.log) -ge 16
diff -u off.log b.log
diff -u off.log a.log

# every process of the DUT is fused in Mode A, and none blocks a Mode A variant
grep -q 'ARR2D_TB.DUT islands=[0-9]* fused_procs=9/9' a_dump.log
grep -q 'VHDL_ISLAND_INFO: .*ARR2D_TB.DUT.* mode=A a_variant=yes' a_dump.log
grep -q 'VHDL_ISLAND_INFO: .*ARR2D_TB.DUT.* mode=B a_variant=yes' b_dump.log
! grep -q 'a_variant=no' b_dump.log
! grep -q 'VHDL_ISLAND_REJECT: .*ARR2D_TB.DUT' a_dump.log
