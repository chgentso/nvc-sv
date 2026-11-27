set -xe

# Loops whose inner loop bounds or slice bounds change from pass to pass,
# through a variable the body updates (a stride doubled on every pass), are
# lowered one pass at a time with the values of the parameter and those
# variables known, so the combinational processes calling them are fused
# into RTL islands.  A value assigned in a branch, in an ordinary loop or
# through a procedure parameter is not known after it.  The output must be
# identical to --rtl-islands=off in Mode B, with --no-island-fast, without
# the activity test, in the speculative Mode A variant and with
# --2state=islands.

cat >unroll1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity unroll_dut is
    port (
        clk : in  std_logic;
        din : in  std_logic_vector(15 downto 0);
        o1  : out std_logic_vector(3 downto 0);
        o2  : out std_logic_vector(3 downto 0);
        o3  : out std_logic_vector(7 downto 0);
        o4  : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of unroll_dut is
    signal q : std_logic_vector(15 downto 0) := (others => '0');

    -- Bit number of a one-hot value: OR of slices whose width doubles
    function bitnum(a : std_logic_vector(15 downto 0)) return std_logic_vector is
        variable ret    : std_logic_vector(3 downto 0);
        variable stride : natural;
        variable k      : natural;
        variable b      : std_logic;
    begin
        stride := 2;
        for i in 0 to 3 loop
            b := '0';
            for j in 0 to (16 / stride) - 1 loop
                k := j * stride;
                b := b or (or a(k + stride - 1 downto k + (stride / 2)));
            end loop;
            ret(i) := b;
            stride := stride * 2;
        end loop;
        return ret;
    end function;

    -- Inner loop bound from the outer parameter and a parameter
    function tri(v : std_logic_vector; n : natural) return std_logic_vector is
        variable p : std_logic_vector(n - 1 downto 0);
        variable b : std_logic;
    begin
        for i in 0 to n - 1 loop
            b := '0';
            for j in 0 to i loop
                b := b xor v(j + i);
            end loop;
            p(i) := b;
        end loop;
        return p;
    end function;

    procedure bump(x : inout natural) is
    begin
        x := x + 1;
    end procedure;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
        end if;
    end process;

    p_bitnum : process (q) is
        variable oh : std_logic_vector(15 downto 0);
    begin
        oh := q and std_logic_vector(unsigned(not q) + 1);   -- Lowest set bit
        o1 <= bitnum(oh);
    end process;

    p_tri : process (q) is
    begin
        o2 <= tri(q(7 downto 0), 4);
    end process;

    -- Values that must not be taken as known
    p_branch : process (q) is
        variable k : natural;
        variable n : natural;
        variable t : std_logic_vector(7 downto 0);
    begin
        k := 1;
        if q(0) = '1' then
            k := 3;
        end if;
        n := 1;
        case q(2 downto 1) is
            when "00" => n := 0;
            when "01" => n := 2;
            when others => null;
        end case;
        if q(2 downto 1) = "10" or q(2 downto 1) = "11" then
            n := 4;
        end if;
        t := q(15 downto 8);
        t(k) := q(n + 3);
        o3 <= t;
    end process;

    p_loop : process (q) is
        variable c : natural;
        variable t : std_logic_vector(7 downto 0);
    begin
        c := 0;
        for i in 0 to 5 loop
            if q(i) = '1' then
                c := c + 1;
            end if;
        end loop;
        bump(c);
        t := (others => '0');
        t(c) := '1';
        o4 <= t;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity unroll_tb is
end entity;

architecture test of unroll_tb is
    signal clk : std_logic := '0';
    signal din : std_logic_vector(15 downto 0) := (others => '0');
    signal o1, o2 : std_logic_vector(3 downto 0);
    signal o3, o4 : std_logic_vector(7 downto 0);
begin
    dut : entity work.unroll_dut port map (clk, din, o1, o2, o3, o4);

    p_log : process (o1, o2, o3, o4) is
    begin
        report "o1=" & to_string(o1) & " o2=" & to_string(o2)
            & " o3=" & to_string(o3) & " o4=" & to_string(o4);
    end process;

    p_stim : process is
        variable v : unsigned(15 downto 0) := x"3c51";
    begin
        for k in 0 to 15 loop
            din <= std_logic_vector(v);
            v := resize(v * 5, 16) + 4097;
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a unroll1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e unroll_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a unroll1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e unroll_tb \
    > b_dump.log
cat b_dump.log
grep -q 'UNROLL_TB.DUT islands=1 fused_procs=5/5' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd unroll_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast unroll_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r unroll_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r unroll_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a unroll1.vhd
NVC_ISLAND_SPEC=1 NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s \
    --rtl-islands=hier -e unroll_tb > s_dump.log
cat s_dump.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r unroll_tb > s.log 2>&1

nvc --std=2008 --work=w_a -a unroll1.vhd
nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e unroll_tb
nvc --std=2008 --work=w_a -r unroll_tb > a.log 2>&1

for m in b bnf b0 bchk s a; do
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
