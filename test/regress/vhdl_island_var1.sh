set -xe

# A process variable keeps its value between runs, so a combinational
# process is only fused when it assigns every part of a variable it reads
# earlier in the same run.  Assigning every element or record field one by
# one, also in a loop with constant bounds, counts as assigning the whole
# variable; a process that reads a field kept from its previous run stays
# unfused.  The output must be identical to --rtl-islands=off in Mode B,
# with --no-island-fast, without the activity test, in the speculative
# Mode A variant and with --2state=islands.

cat >var1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity var_dut is
    port (
        clk  : in  std_logic;
        din  : in  std_logic_vector(7 downto 0);
        dout : out std_logic_vector(7 downto 0);
        rout : out std_logic_vector(7 downto 0);
        kout : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of var_dut is
    type inner_t is record
        a : std_logic;
        b : std_logic_vector(2 downto 0);
    end record;
    type rec_t is record
        x : std_logic_vector(3 downto 0);
        y : inner_t;
        z : unsigned(3 downto 0);
    end record;
    signal q, d1, d2, d3, d4 : std_logic_vector(7 downto 0) := (others => '0');
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
        end if;
    end process;

    -- Every element assigned one by one before the whole vector is read
    p_elems : process (all) is
        variable e : std_logic_vector(3 downto 0);
    begin
        e(3) := q(0);
        e(2) := q(1);
        e(1 downto 0) := q(3 downto 2);
        e(0) := e(0) xor e(3);    -- Partial write of an assigned element
        d1 <= q(7 downto 4) & e;
    end process;

    -- Record fields assigned one by one, a nested field, then read whole
    p_rec : process (all) is
        variable r : rec_t;
    begin
        r.x := q(3 downto 0);
        r.y.a := q(4);
        if q(5) = '1' then
            r.y.b := q(7 downto 5);
            r.z := unsigned(q(3 downto 0)) + 1;
        else
            r.y.b := "000";
            r.z := unsigned(q(7 downto 4));
        end if;
        r.y.b(0) := r.y.a;
        d2 <= r.x & std_logic_vector(r.z xor unsigned(r.y.b & r.y.a));
    end process;

    -- Every pair of bits assigned in a loop with constant bounds
    p_loop : process (all) is
        variable s : std_logic_vector(7 downto 0);
    begin
        for i in 0 to 3 loop
            if q(i) = '1' then
                s(i * 2 + 1 downto i * 2) := q(7 - i) & q(i);
            else
                s(i * 2 + 1 downto i * 2) := "10";
            end if;
        end loop;
        d4 <= s;
    end process;

    -- Keeps a field between runs: must stay unfused
    p_keep : process (all) is
        variable k : rec_t := ("0000", ('0', "000"), "0000");
    begin
        k.x := q(3 downto 0);
        d3 <= k.x & std_logic_vector(k.z);
        k.z := unsigned(q(7 downto 4));
    end process;

    dout <= d1;
    rout <= d2;
    kout <= d3 xor d4;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity var_tb is
end entity;

architecture test of var_tb is
    signal clk  : std_logic := '0';
    signal din  : std_logic_vector(7 downto 0) := (others => '0');
    signal dout, rout, kout : std_logic_vector(7 downto 0);
begin
    dut : entity work.var_dut port map (clk, din, dout, rout, kout);

    p_log : process (dout, rout, kout) is
    begin
        report "dout=" & to_hstring(dout) & " rout=" & to_hstring(rout)
            & " kout=" & to_hstring(kout);
    end process;

    p_stim : process is
        variable v : unsigned(7 downto 0) := x"3c";
    begin
        for k in 0 to 9 loop
            din <= std_logic_vector(v);
            v := resize(v * 5, 8) + 17;
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a var1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e var_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a var1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e var_tb \
    > b_dump.log
cat b_dump.log
grep -q 'VAR_TB.DUT islands=1 fused_procs=7/8' b_dump.log
grep -q 'P_KEEP reason=var: read before assignment of K' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd var_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast var_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r var_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r var_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a var1.vhd
NVC_ISLAND_SPEC=1 nvc --std=2008 --work=w_s --rtl-islands=hier -e var_tb
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r var_tb > s.log 2>&1

nvc --std=2008 --work=w_a -a var1.vhd
nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e var_tb
nvc --std=2008 --work=w_a -r var_tb > a.log 2>&1

for m in b bnf b0 bchk s a; do
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
