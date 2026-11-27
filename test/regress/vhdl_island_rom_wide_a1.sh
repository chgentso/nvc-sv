set -xe

# Constant ROMs with rows wider than 64 bits and functions returning an
# unconstrained vector in Mode A (--2state=islands): the output must equal
# the Mode B and --rtl-islands=off runs.

cat >rom_wide_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rom_wide_dut is
    port (
        clk  : in  std_logic;
        addr : in  unsigned(2 downto 0);
        din  : in  std_logic_vector(15 downto 0);
        sw   : in  std_logic;
        row  : out std_logic_vector(95 downto 0);
        low  : out std_logic_vector(15 downto 0);
        top  : out std_logic_vector(7 downto 0);
        pk8  : out std_logic_vector(7 downto 0);
        pk16 : out std_logic_vector(15 downto 0);
        q    : out std_logic_vector(95 downto 0) );
end entity;

architecture rtl of rom_wide_dut is
    type rom_t is array (0 to 7) of std_logic_vector(95 downto 0);
    constant ROM : rom_t := (
        x"0123456789abcdef01234567", x"fedcba9876543210fedcba98",
        x"00000000ffffffff00000000", x"ffffffff00000000ffffffff",
        x"a5a5a5a5a5a5a5a5a5a5a5a5", x"5a5a5a5a5a5a5a5a5a5a5a5a",
        x"8000000000000000000000f1", x"0f0f0f0f0f0f0f0f0f0f0f0e" );
    type rom72_t is array (0 to 3) of unsigned(71 downto 0);
    constant ROM72 : rom72_t := (
        x"112233445566778899", x"ffeeddccbbaa998877",
        x"000000000000000001", x"800000000000000000" );
    signal r, qr : std_logic_vector(95 downto 0) := (others => '0');

    function inv(x : std_logic_vector) return std_logic_vector is
    begin
        return not x;
    end function;

    function pick(x, y : std_logic_vector; s : boolean)
        return std_logic_vector is
    begin
        if s then
            return x;
        else
            return y;
        end if;
    end function;
begin
    p_rom : process (addr, din, sw) is
        variable t : unsigned(71 downto 0);
    begin
        r   <= ROM(to_integer(addr));
        low <= ROM(3)(15 downto 0) xor din;
        t := ROM72(to_integer(addr(1 downto 0)));
        top <= std_logic_vector(t(71 downto 64));
        pk8  <= pick(inv(din(7 downto 0)), din(15 downto 8), sw = '1');
        pk16 <= pick(din, inv(din), sw = '0');
    end process;

    row <= r;

    p_reg : process (clk) is
    begin
        if rising_edge(clk) then
            qr <= r;
        end if;
    end process;

    q <= qr;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity rom_wide_tb is
end entity;

architecture test of rom_wide_tb is
    signal clk : std_logic := '0';
    signal addr : unsigned(2 downto 0) := "000";
    signal din, low, pk16 : std_logic_vector(15 downto 0) := (others => '0');
    signal sw : std_logic := '0';
    signal row, q : std_logic_vector(95 downto 0);
    signal top, pk8 : std_logic_vector(7 downto 0);
begin
    dut : entity work.rom_wide_dut
        port map (clk, addr, din, sw, row, low, top, pk8, pk16, q);

    stim : process is
        variable d : unsigned(15 downto 0) := x"c0de";
    begin
        for k in 0 to 15 loop
            addr <= to_unsigned(k mod 8, 3);
            sw <= '1' when k mod 3 = 0 else '0';
            d := d + x"0f3b";
            din <= std_logic_vector(d);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            report integer'image(k) & " " & to_hstring(row) & " "
               & to_hstring(low) & " " & to_hstring(top) & " "
               & to_hstring(pk8) & " " & to_hstring(pk16) & " "
               & to_hstring(q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a rom_wide_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e rom_wide_tb -r 2>&1 | sort > off.log

nvc --std=2008 --work=w_b -a rom_wide_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e rom_wide_tb >b_dump.log
nvc --std=2008 --work=w_b -r rom_wide_tb 2>&1 | sort > b.log

nvc --std=2008 --work=w_a -a rom_wide_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e rom_wide_tb >a_dump.log
nvc --std=2008 --work=w_a -r rom_wide_tb 2>&1 | sort > a.log

cat a_dump.log b_dump.log
test $(grep -c ' 0123456789ABCDEF01234567 ' off.log) -ge 1
diff -u off.log b.log
diff -u off.log a.log

# all processes of the DUT are fused in both modes
grep -q 'ROM_WIDE_TB.DUT islands=[0-9]* fused_procs=4/4' a_dump.log
grep -q 'ROM_WIDE_TB.DUT islands=[0-9]* fused_procs=4/4' b_dump.log
! grep -q 'a_variant=no' b_dump.log
