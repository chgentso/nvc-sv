set -xe

# Wide (> 64 bit) local variables that are partly assigned before they are
# fully assigned: in Mode A (--2state=islands) the variable has no value until
# its first store, so the first slice insert read a null pointer.  Results must
# equal the Mode B and --rtl-islands=off runs.

cat >widevar_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity widevar_dut is
    port (
        din  : in  std_logic_vector(255 downto 0);
        o1   : out std_logic_vector(255 downto 0);
        o2   : out std_logic_vector(255 downto 0);
        o3   : out std_logic_vector(15 downto 0) );
end entity;

architecture rtl of widevar_dut is
    type rec_t is record
        a : std_logic_vector(255 downto 0);
        b : std_logic_vector(3 downto 0);
    end record;
    signal m1, m3 : std_logic_vector(255 downto 0) := (others => '0');

    function build(x : std_logic_vector(255 downto 0))
        return std_logic_vector is
        variable t : std_logic_vector(255 downto 0);
    begin
        t(255 downto 192) := x(63 downto 0);
        t(191 downto 128) := x(127 downto 64);
        t(127 downto 64)  := x(191 downto 128);
        t(63 downto 0)    := x(255 downto 192);
        t(79 downto 76)   := x"c";
        return t;
    end function;

    function pick(x : std_logic_vector(255 downto 0))
        return std_logic_vector is
        variable r : rec_t;
    begin
        r.a(255 downto 128) := x(127 downto 0);
        r.a(127 downto 0)   := x(255 downto 128);
        r.a(7 downto 4)     := x"9";
        return r.a;
    end function;
begin
    p0 : process (din) is
    begin
        m1 <= not din;
        m3 <= din(127 downto 0) & din(255 downto 128);
    end process;

    p1 : process (m1, m3) is
    begin
        o1 <= build(m1);
        o2 <= pick(m3);
        o3 <= build(m3)(100 downto 85);
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity widevar_tb is end entity;

architecture t of widevar_tb is
    signal din : std_logic_vector(255 downto 0) := (others => '0');
    signal o1, o2 : std_logic_vector(255 downto 0);
    signal o3 : std_logic_vector(15 downto 0);
begin
    dut : entity work.widevar_dut port map (din, o1, o2, o3);

    stim : process is
    begin
        for i in 0 to 7 loop
            din <= (others => '0');
            din(i * 8 + 7 downto i * 8) <= x"f3";
            din(255 downto 248) <= x"81";
            wait for 1 ns;
            report integer'image(i) & " " & to_hstring(o1) & " "
               & to_hstring(o2) & " " & to_hstring(o3);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a widevar_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e widevar_tb -r 2>&1 | sort > off.log

nvc --std=2008 --work=w_b -a widevar_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e widevar_tb >b_dump.log
nvc --std=2008 --work=w_b -r widevar_tb 2>&1 | sort > b.log

nvc --std=2008 --work=w_a -a widevar_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e widevar_tb >a_dump.log
nvc --std=2008 --work=w_a -r widevar_tb 2>&1 | sort > a.log

cat a_dump.log
test $(wc -l < off.log) -ge 8
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log b.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log a.log

grep -q 'WIDEVAR_TB.DUT islands=[0-9]* fused_procs=2/2' a_dump.log
grep -q 'VHDL_ISLAND_INFO: .*WIDEVAR_TB.DUT.* mode=A a_variant=yes' a_dump.log
