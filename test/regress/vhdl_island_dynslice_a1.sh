set -xe

# Dynamic slices (constant width, variable position) and to_hstring
# of vectors in Mode A (--2state=islands): the output must equal the Mode B
# and --rtl-islands=off runs.

cat >dynslice_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dynslice_dut is
    port (
        clk  : in  std_logic;
        sel  : in  unsigned(2 downto 0);
        din  : in  std_logic_vector(31 downto 0);
        rd   : out std_logic_vector(7 downto 0);
        lv   : out std_logic_vector(31 downto 0);
        sgo  : out std_logic_vector(31 downto 0);
        rowo : out std_logic_vector(7 downto 0);
        cnt  : out unsigned(7 downto 0) );
end entity;

architecture rtl of dynslice_dut is
    type rows_t is array (0 to 3) of std_logic_vector(15 downto 0);
    signal sg : std_logic_vector(31 downto 0) := (others => '0');
    signal rw : rows_t := (others => (others => '0'));
    signal n  : unsigned(7 downto 0) := (others => '0');
begin
    p_ds : process (sel, din) is
        variable v : std_logic_vector(31 downto 0);
        variable m : rows_t;
        variable i, j : integer;
    begin
        i := to_integer(sel) * 4;
        j := to_integer(sel(0 downto 0)) * 4;
        v := din;
        v(i + 3 downto i) := not din(3 downto 0);
        rd <= din(i + 7 downto i);
        lv <= v;
        sg(i + 3 downto i) <= din(7 downto 4);
        m := (others => din(15 downto 0));
        m(to_integer(sel(1 downto 0)))(j + 3 downto j) := "1010";
        m(1)(j + 1 downto j) := "11";
        rowo <= m(to_integer(sel(1 downto 0)))(j + 7 downto j);
        rw <= m;
    end process;

    sgo <= sg;

    p_cnt : process (clk) is
    begin
        if rising_edge(clk) then
            n <= n + 1;
            if sel = "011" then
                report "n=" & to_hstring(n) & " d=" & to_hstring(din)
                   & " b=" & to_hstring(sel) & " " & to_hstring(rw(1));
            end if;
        end if;
    end process;
    cnt <= n;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dynslice_tb is
end entity;

architecture test of dynslice_tb is
    signal clk : std_logic := '0';
    signal sel : unsigned(2 downto 0) := "000";
    signal din, lv, sgo : std_logic_vector(31 downto 0) := (others => '0');
    signal rd, rowo : std_logic_vector(7 downto 0);
    signal cnt : unsigned(7 downto 0);
begin
    dut : entity work.dynslice_dut
        port map (clk, sel, din, rd, lv, sgo, rowo, cnt);

    stim : process is
        variable d : unsigned(31 downto 0) := x"89abcdef";
    begin
        for k in 0 to 15 loop
            sel <= to_unsigned(k mod 6, 3);
            d := resize(d * x"00010001", 32) + x"1234567";
            din <= std_logic_vector(d);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            report integer'image(k) & " " & to_hstring(rd) & " "
               & to_hstring(lv) & " " & to_hstring(sgo) & " "
               & to_hstring(rowo) & " " & to_hstring(cnt);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a dynslice_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e dynslice_tb -r 2>&1 | sort > off.log

nvc --std=2008 --work=w_b -a dynslice_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e dynslice_tb >b_dump.log
nvc --std=2008 --work=w_b -r dynslice_tb 2>&1 | sort > b.log

nvc --std=2008 --work=w_a -a dynslice_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e dynslice_tb >a_dump.log
nvc --std=2008 --work=w_a -r dynslice_tb 2>&1 | sort > a.log

cat a_dump.log b_dump.log
grep -q 'n=0[0-9A-F] d=' off.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log b.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log a.log

# all processes of the DUT are fused in both modes
grep -q 'DYNSLICE_TB.DUT islands=[0-9]* fused_procs=4/4' a_dump.log
grep -q 'DYNSLICE_TB.DUT islands=[0-9]* fused_procs=4/4' b_dump.log
! grep -q 'a_variant=no' b_dump.log
