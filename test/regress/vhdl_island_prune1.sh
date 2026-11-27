set -xe

# A combinational process alone in its island, with no dataflow inside the
# island, is not fused when it writes few signals: the island would only
# add input syncing and output commits.  One that writes many (here every
# field of two record outputs on each run) is fused: the island commits
# only the outputs that change.  The output must be identical to
# --rtl-islands=off.

cat >prune1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package prune_pkg is
    type wb_t is record
        valid : std_logic;
        we    : std_logic;
        sel   : std_logic_vector(3 downto 0);
        adr   : unsigned(7 downto 0);
        dat   : std_logic_vector(7 downto 0);
        tag   : natural range 0 to 15;
        a, b, c, d, e, f, g, h : std_logic;
    end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.prune_pkg.all;

entity prune_dut is
    port (
        din : in  std_logic_vector(7 downto 0);
        o1  : out wb_t;
        o2  : out wb_t;
        o3  : out std_logic );
end entity;

architecture rtl of prune_dut is
begin
    -- Writes 28 signals: fused
    p_wide : process (din) is
        variable v : wb_t;
    begin
        v.valid := din(0);
        v.we := din(1);
        v.sel := din(5 downto 2);
        v.adr := unsigned(din);
        v.dat := not din;
        v.tag := to_integer(unsigned(din(7 downto 4)));
        v.a := din(0) xor din(1);
        v.b := din(1) xor din(2);
        v.c := din(2) xor din(3);
        v.d := din(3) xor din(4);
        v.e := din(4) xor din(5);
        v.f := din(5) xor din(6);
        v.g := din(6) xor din(7);
        v.h := '0';
        o1 <= v;
        v.valid := '0';
        v.adr := v.adr + 1;
        o2 <= v;
    end process;

    -- Writes one signal: stays a process
    p_narrow : process (din) is
    begin
        o3 <= din(0) and din(7);
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.prune_pkg.all;

entity prune_tb is
end entity;

architecture test of prune_tb is
    signal din : std_logic_vector(7 downto 0) := (others => '0');
    signal o1, o2 : wb_t;
    signal o3 : std_logic;
begin
    dut : entity work.prune_dut port map (din, o1, o2, o3);

    p_log : process (o1, o2, o3) is
    begin
        report "o1=" & to_string(o1.valid) & to_string(o1.we)
            & to_string(o1.sel) & " " & to_hstring(o1.adr) & " "
            & to_hstring(o1.dat) & " " & integer'image(o1.tag) & " "
            & to_string(o1.a) & to_string(o1.b) & to_string(o1.c)
            & to_string(o1.d) & to_string(o1.e) & to_string(o1.f)
            & to_string(o1.g) & to_string(o1.h)
            & " o2=" & to_string(o2.valid) & " " & to_hstring(o2.adr)
            & " o3=" & to_string(o3);
    end process;

    p_stim : process is
        variable v : unsigned(7 downto 0) := x"3c";
    begin
        for k in 0 to 11 loop
            din <= std_logic_vector(v);
            if k mod 3 = 2 then
                v := resize(v * 5, 8) + 17;
            end if;
            wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a prune1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e prune_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a prune1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e prune_tb \
    > b_dump.log
cat b_dump.log
grep -q 'PRUNE_TB.DUT islands=1 fused_procs=1/2' b_dump.log
grep -q 'P_NARROW reason=prune: comb-only island' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd prune_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast prune_tb > bnf.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r prune_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a prune1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e prune_tb
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r prune_tb > s.log 2>&1

for m in b bnf bchk s; do
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
