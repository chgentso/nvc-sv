set -xe

# Combinational processes that call functions returning a record are fused
# into RTL islands: the call is inlined into a record of its own and copied
# to the variable, field or signal it is assigned to.  Covers nested records,
# an array of records, a function with variables, a case and a loop inside,
# a return from inside a loop, a record result passed to another function
# and T'VAL of an enumeration.  The output must be identical to
# --rtl-islands=off in Mode B, with --no-island-fast, without the activity
# test, in the speculative Mode A variant and with --2state=islands, also
# the error of a T'VAL out of range.

cat >recfunc1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package recfunc_pkg is
    type op_t is (op_nop, op_add, op_sub, op_and, op_or, op_xor);
    type inner_t is record
        sel  : std_logic_vector(1 downto 0);
        flag : std_logic;
    end record;
    type dec_t is record
        op    : op_t;
        inner : inner_t;
        imm   : unsigned(3 downto 0);
        count : natural range 0 to 8;
    end record;
    type pair_t is array (0 to 1) of inner_t;
    type both_t is record
        p : pair_t;
        n : integer range -8 to 7;
    end record;

    function decode(x : std_logic_vector(7 downto 0)) return dec_t;
    function first_one(x : std_logic_vector(7 downto 0)) return inner_t;
    function swap(b : both_t) return both_t;
    function score(d : dec_t) return unsigned;
end package;

package body recfunc_pkg is
    function decode(x : std_logic_vector(7 downto 0)) return dec_t is
        variable d : dec_t;
        variable n : natural range 0 to 8;
    begin
        d.inner.sel := x(1 downto 0);
        d.inner.flag := '0';
        d.imm := unsigned(x(7 downto 4));
        case x(3 downto 2) is
            when "00" => d.op := op_nop;
            when "01" => d.op := op_add;
                         d.inner.flag := '1';
            when "10" => d.op := op_sub;
            when others =>
                if x(0) = '1' then
                    d.op := op_xor;
                else
                    d.op := op_or;
                end if;
        end case;
        n := 0;
        for i in x'range loop
            if x(i) = '1' then
                n := n + 1;
            end if;
        end loop;
        d.count := n;
        return d;
    end function;

    function first_one(x : std_logic_vector(7 downto 0)) return inner_t is
    begin
        for i in 0 to 3 loop
            if x(i + 4) = '1' then
                return (sel => std_logic_vector(to_unsigned(i, 2)),
                        flag => '1');
            end if;
        end loop;
        return (sel => "00", others => '0');
    end function;

    function swap(b : both_t) return both_t is
        variable r : both_t;
    begin
        r.p(0) := b.p(1);
        r.p(1) := b.p(0);
        r.n := -b.n;
        return r;
    end function;

    function score(d : dec_t) return unsigned is
    begin
        return d.imm + to_unsigned(d.count, 4);
    end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.recfunc_pkg.all;

entity recfunc_dut is
    port (
        clk   : in  std_logic;
        din   : in  std_logic_vector(7 downto 0);
        d_out : out dec_t;
        f_out : out inner_t;
        s_out : out both_t;
        c_out : out unsigned(3 downto 0);
        e_out : out op_t );
end entity;

architecture rtl of recfunc_dut is
    type wrap_t is record
        tag : std_logic;
        dec : dec_t;
    end record;
    signal q : std_logic_vector(7 downto 0) := (others => '0');
    signal w : wrap_t := ('0', (op_nop, ("00", '0'), x"0", 0));
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= din;
        end if;
    end process;

    -- Field of a record variable from a call, the variable then copied
    p_field : process (q) is
        variable v : wrap_t;
    begin
        v.tag := q(7);
        v.dec := decode(q);
        w <= v;
    end process;

    -- Signal assigned from a call directly
    p_sig : process (q) is
    begin
        f_out <= first_one(q);
    end process;

    -- Array of records in a record, through a variable
    p_arr : process (q) is
        variable b : both_t;
    begin
        b.p(0) := (sel => q(1 downto 0), flag => q(2));
        b.p(1) := (sel => q(4 downto 3), flag => q(5));
        b.n := to_integer(signed(q(7 downto 5))) mod 8 - 4;
        s_out <= swap(b);
    end process;

    -- A record result passed as a parameter
    p_nest : process (q) is
    begin
        c_out <= score(decode(q));
    end process;

    -- T'VAL of an enumeration
    p_val : process (q) is
    begin
        e_out <= op_t'val(to_integer(unsigned(q(1 downto 0))) + 1);
    end process;

    d_out <= w.dec;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.recfunc_pkg.all;

entity recfunc_tb is
end entity;

architecture test of recfunc_tb is
    signal clk   : std_logic := '0';
    signal din   : std_logic_vector(7 downto 0) := (others => '0');
    signal d_out : dec_t;
    signal f_out : inner_t;
    signal s_out : both_t;
    signal c_out : unsigned(3 downto 0);
    signal e_out : op_t;
begin
    dut : entity work.recfunc_dut port map (clk, din, d_out, f_out, s_out,
                                            c_out, e_out);

    p_log : process (d_out, f_out, s_out, c_out, e_out) is
    begin
        report "op=" & op_t'image(d_out.op)
            & " sel=" & to_string(d_out.inner.sel)
            & " flag=" & to_string(d_out.inner.flag)
            & " imm=" & to_hstring(d_out.imm)
            & " count=" & integer'image(d_out.count)
            & " first=" & to_string(f_out.sel) & to_string(f_out.flag)
            & " swap=" & to_string(s_out.p(0).sel) & to_string(s_out.p(0).flag)
            & to_string(s_out.p(1).sel) & to_string(s_out.p(1).flag)
            & " n=" & integer'image(s_out.n)
            & " score=" & to_hstring(c_out)
            & " val=" & op_t'image(e_out);
    end process;

    p_stim : process is
        variable v : unsigned(7 downto 0) := x"3c";
    begin
        for k in 0 to 11 loop
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

nvc --std=2008 --work=w_off -a recfunc1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e recfunc_tb \
    -r --wave=off.vcd --format=vcd --dump-arrays > off.log 2>&1
grep -q "done" off.log

nvc --std=2008 --work=w_b -a recfunc1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e recfunc_tb \
    > b_dump.log
cat b_dump.log
grep -q 'RECFUNC_TB.DUT islands=1 fused_procs=7/7' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd --dump-arrays recfunc_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast recfunc_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r recfunc_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r recfunc_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a recfunc1.vhd
NVC_ISLAND_SPEC=1 NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s \
    --rtl-islands=hier -e recfunc_tb > s_dump.log
cat s_dump.log
grep -q 'RECFUNC_TB.DUT island=0 .*a_variant=yes' s_dump.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r recfunc_tb > s.log 2>&1

nvc --std=2008 --work=w_a -a recfunc1.vhd
nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e recfunc_tb
nvc --std=2008 --work=w_a -r recfunc_tb > a.log 2>&1

for m in b bnf b0 bchk s a; do
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s

cat >valerr.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity valerr is
end entity;

architecture test of valerr is
    type col_t is (red, green, blue);
    subtype warm_t is col_t range red to green;
    signal clk : std_logic := '0';
    signal d, q : unsigned(1 downto 0) := "00";
    signal c : warm_t;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
        end if;
    end process;

    p_val : process (q) is
    begin
        c <= warm_t'val(to_integer(q));
    end process;

    p_stim : process is
    begin
        for k in 0 to 3 loop
            d <= to_unsigned(k, 2);
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
            report col_t'image(c);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=v_off -a valerr.vhd
! nvc --std=2008 --work=v_off --rtl-islands=off -e valerr -r > voff.log 2>&1
nvc --std=2008 --work=v_b -a valerr.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=v_b --rtl-islands=hier -e valerr \
    > v_dump.log
cat v_dump.log
grep -q 'VALERR islands=1 fused_procs=2/3' v_dump.log
! nvc --std=2008 --work=v_b -r valerr > vb.log 2>&1
cat voff.log
grep -q "value BLUE outside of WARM_T range RED to GREEN for attribute 'VAL" voff.log
diff -u voff.log vb.log
