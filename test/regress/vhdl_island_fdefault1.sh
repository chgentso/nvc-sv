set -xe

# A variable of a subprogram inlined into an RTL island that has no initial
# value starts as T'LEFT of its type ('U', integer'low, the first literal),
# as with --rtl-islands=off, also where the subprogram reads it before
# assigning all of it.  The speculative Mode A variant cannot hold 'U': an
# island whose subprograms may read such a default has no A variant, one
# whose subprograms assign their variables first (whole, field by field, in
# every branch of an if or case, or element by element in a loop) keeps it.

cat >fdefault1.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package fdef_pkg is
    type colour_t is (red, green, blue);
    type pair_t is record
        a : std_logic;
        b : std_logic_vector(2 downto 0);
        n : natural;
    end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.fdef_pkg.all;

-- Subprograms that read defaults
entity fdef_u is
    port (
        clk  : in  std_logic;
        din  : in  std_logic_vector(3 downto 0);
        v    : out std_logic_vector(3 downto 0);
        s    : out std_logic;
        i    : out integer;
        c    : out colour_t;
        p    : out std_logic_vector(3 downto 0);
        q    : out std_logic_vector(3 downto 0) );
end entity;

architecture rtl of fdef_u is
    signal dq : std_logic_vector(3 downto 0) := "0000";
    function part(x : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable r : std_logic_vector(3 downto 0);
    begin
        r(0) := x(0);
        if x(1) = '1' then
            r(2) := x(2);
        end if;
        return r;
    end function;

    function scal(x : std_logic) return std_logic is
        variable r : std_logic;
    begin
        if x = '1' then
            r := '0';
        end if;
        return r;
    end function;

    function int_left(x : std_logic) return integer is
        variable n : integer;
        variable k : natural;
    begin
        if x = '1' then
            return k + 5;
        end if;
        return n;
    end function;

    function enum_left(x : std_logic) return colour_t is
        variable e : colour_t;
    begin
        if x = '1' then
            e := blue;
        end if;
        return e;
    end function;

    function rec_part(x : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable r : pair_t;
    begin
        r.a := x(3);
        return r.a & r.b;
    end function;

    procedure proc_part(x : in std_logic_vector(3 downto 0);
                        y : out std_logic_vector(3 downto 0)) is
        variable t : std_logic_vector(3 downto 0);
    begin
        t(3) := x(0);
        y := t;
    end procedure;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            dq <= din;
        end if;
    end process;

    p_v : process (dq) is
    begin
        v <= part(dq);
    end process;

    p_s : process (dq) is
    begin
        s <= scal(dq(0));
    end process;

    p_i : process (dq) is
    begin
        i <= int_left(dq(1));
    end process;

    p_c : process (dq) is
    begin
        c <= enum_left(dq(2));
    end process;

    p_p : process (dq) is
    begin
        p <= rec_part(dq);
    end process;

    p_q : process (dq) is
        variable y : std_logic_vector(3 downto 0);
    begin
        proc_part(dq, y);
        q <= y;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.fdef_pkg.all;

-- Subprograms that assign their variables before reading them
entity fdef_d is
    port (
        clk  : in  std_logic;
        din  : in  std_logic_vector(3 downto 0);
        r1   : out std_logic_vector(3 downto 0);
        r2   : out std_logic_vector(3 downto 0);
        r3   : out std_logic_vector(3 downto 0);
        r4   : out std_logic );
end entity;

architecture rtl of fdef_d is
    signal dq : std_logic_vector(3 downto 0) := "0000";
    function rev(x : std_logic_vector) return std_logic_vector is
        variable r : std_logic_vector(x'length - 1 downto 0);
    begin
        for i in 0 to x'length - 1 loop
            r(x'length - 1 - i) := x(x'low + i);
        end loop;
        return r;
    end function;

    function sel(x : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable r : std_logic_vector(3 downto 0);
    begin
        case x(1 downto 0) is
            when "00" => r := x;
            when "01" => r := not x;
            when others =>
                r(3 downto 2) := x(1 downto 0);
                r(1 downto 0) := x(3 downto 2);
        end case;
        return r;
    end function;

    function fields(x : std_logic_vector(3 downto 0)) return std_logic_vector is
        variable p : pair_t;
    begin
        p.a := x(0);
        if x(1) = '1' then
            p.b := x(3 downto 1);
        else
            p.b(2) := x(3);
            p.b(1 downto 0) := "01";
        end if;
        p.n := 1;
        return p.a & p.b(2) & p.b(0) & x(p.n);
    end function;

    function red(x : std_logic_vector(3 downto 0)) return std_logic is
        variable v : std_logic;
    begin
        v := '0';
        for i in x'range loop
            v := v xor x(i);
        end loop;
        return v;
    end function;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            dq <= din;
        end if;
    end process;

    p_1 : process (dq) is
    begin
        r1 <= rev(dq);
    end process;

    p_2 : process (dq) is
    begin
        r2 <= sel(dq);
    end process;

    p_3 : process (dq) is
    begin
        r3 <= fields(dq);
    end process;

    p_4 : process (dq) is
    begin
        r4 <= red(dq);
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.fdef_pkg.all;

entity fdef_tb is
end entity;

architecture test of fdef_tb is
    signal din : std_logic_vector(3 downto 0) := (others => '0');
    signal clk : std_logic := '0';
    signal v, p, q, r1, r2, r3 : std_logic_vector(3 downto 0);
    signal s, r4 : std_logic;
    signal i : integer;
    signal c : colour_t;
begin
    u : entity work.fdef_u port map (clk, din, v, s, i, c, p, q);
    d : entity work.fdef_d port map (clk, din, r1, r2, r3, r4);

    p_log : process (v, s, i, c, p, q, r1, r2, r3, r4) is
    begin
        report "v=" & to_string(v) & " s=" & to_string(s)
            & " i=" & integer'image(i) & " c=" & colour_t'image(c)
            & " p=" & to_string(p) & " q=" & to_string(q)
            & " r1=" & to_string(r1) & " r2=" & to_string(r2)
            & " r3=" & to_string(r3) & " r4=" & to_string(r4);
    end process;

    p_stim : process is
    begin
        for k in 0 to 15 loop
            din <= std_logic_vector(to_unsigned((k * 7 + 3) mod 16, 4));
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;
        end loop;
        report "done";
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a fdefault1.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e fdef_tb \
    -r --wave=off.vcd --format=vcd > off.log 2>&1
grep -q "done" off.log
grep -q "v=UUU0 s=U i=-2147483648 c=red p=0UUU q=0UUU" off.log

nvc --std=2008 --work=w_b -a fdefault1.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e fdef_tb \
    > b_dump.log
cat b_dump.log
grep -q 'FDEF_TB.U islands=1 fused_procs=7/7' b_dump.log
grep -q 'FDEF_TB.D islands=1 fused_procs=5/5' b_dump.log

nvc --std=2008 --work=w_b -r --wave=b.vcd --format=vcd fdef_tb > b.log 2>&1
nvc --std=2008 --work=w_b -r --no-island-fast fdef_tb > bnf.log 2>&1
NVC_ISLAND_ACT=0 nvc --std=2008 --work=w_b -r fdef_tb > b0.log 2>&1
NVC_ISLAND_ACT_CHECK=1 nvc --std=2008 --work=w_b -r fdef_tb > bchk.log 2>&1

nvc --std=2008 --work=w_s -a fdefault1.vhd
NVC_ISLAND_SPEC=1 NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_s \
    --rtl-islands=hier -e fdef_tb > s_dump.log
cat s_dump.log
grep -q 'FDEF_TB.U island=0 .*a_variant=no' s_dump.log
grep -q 'FDEF_TB.D island=0 .*a_variant=yes' s_dump.log
NVC_ISLAND_SPEC_FORCE=A nvc --std=2008 --work=w_s -r fdef_tb > s.log 2>&1

for m in b bnf b0 bchk s; do
    sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
    diff -u off.log $m.log
done
sed '/^\$date/,/\$end/d' off.vcd > off.vcd.s
sed '/^\$date/,/\$end/d' b.vcd > b.vcd.s
cmp off.vcd.s b.vcd.s
