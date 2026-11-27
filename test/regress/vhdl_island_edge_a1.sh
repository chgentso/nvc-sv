set -xe

# Edges of a Mode A island (--2state=islands): FF commit of narrow, one-word,
# multi-word and array registers, packing of byte-per-bit signals into the
# two-state words (all nine std_logic values) and unpacking back, for vector
# widths around the byte and word boundaries.  With 0/1 stimuli the output
# must equal Mode B and --rtl-islands=off; with all nine values Mode A is
# lossy by definition: 'H' reads as '1' and every other non-0/1 value as '0'.

cat >edge_a.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

package edge_pkg is
    type arr_t is array (0 to 2) of std_logic_vector(5 downto 0);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.edge_pkg.all;

entity edge_dut is
    port (
        clk                        : in  std_logic;
        d1                         : in  std_logic;
        d3                         : in  std_logic_vector(2 downto 0);
        d7                         : in  std_logic_vector(6 downto 0);
        d8                         : in  std_logic_vector(7 downto 0);
        d9                         : in  std_logic_vector(8 downto 0);
        d17                        : in  std_logic_vector(16 downto 0);
        d33                        : in  std_logic_vector(32 downto 0);
        d64                        : in  std_logic_vector(63 downto 0);
        d65                        : in  std_logic_vector(64 downto 0);
        d130                       : in  std_logic_vector(129 downto 0);
        dup                        : in  std_logic_vector(0 to 11);
        dbv                        : in  bit_vector(12 downto 0);
        darr                       : in  arr_t;
        q1                         : out std_logic;
        q3                         : out std_logic_vector(2 downto 0);
        q7                         : out std_logic_vector(6 downto 0);
        q8                         : out std_logic_vector(7 downto 0);
        q9                         : out std_logic_vector(8 downto 0);
        q17                        : out std_logic_vector(16 downto 0);
        q33                        : out std_logic_vector(32 downto 0);
        q64                        : out std_logic_vector(63 downto 0);
        q65                        : out std_logic_vector(64 downto 0);
        q130                       : out std_logic_vector(129 downto 0);
        qup                        : out std_logic_vector(0 to 11);
        qbv                        : out bit_vector(12 downto 0);
        qarr                       : out arr_t;
        c1                         : out std_logic;
        c7                         : out std_logic_vector(6 downto 0);
        c17                        : out std_logic_vector(16 downto 0);
        c65                        : out std_logic_vector(64 downto 0);
        c130                       : out std_logic_vector(129 downto 0);
        cbv                        : out bit_vector(12 downto 0);
        carr                       : out arr_t );
end entity;

architecture rtl of edge_dut is
    signal r1   : std_logic;
    signal r7   : std_logic_vector(6 downto 0);
    signal r17  : std_logic_vector(16 downto 0);
    signal r65  : std_logic_vector(64 downto 0);
    signal r130 : std_logic_vector(129 downto 0);
    signal rbv  : bit_vector(12 downto 0);
    signal rarr : arr_t;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q1 <= d1;   q3 <= d3;   q7 <= d7;   q8 <= d8;   q9 <= d9;
            q17 <= d17; q33 <= d33; q64 <= d64; q65 <= d65; q130 <= d130;
            qup <= dup; qbv <= dbv; qarr <= darr;
            r1 <= d1; r7 <= d7; r17 <= d17; r65 <= d65; r130 <= d130;
            rbv <= dbv; rarr <= darr;
        end if;
    end process;

    -- combinational outputs computed from registers of the island: the
    -- results are unpacked on exit
    c1   <= not r1;
    c7   <= not r7;
    c17  <= not r17;
    c65  <= not r65;
    c130 <= not r130;
    cbv  <= not rbv;
    p_arr : process (rarr) is
    begin
        for i in rarr'range loop
            carr(i) <= not rarr(i);
        end loop;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.edge_pkg.all;

entity edge_tb is
    generic ( LOSSY : boolean := false; NINE : boolean := false );
end entity;

architecture test of edge_tb is
    signal clk : std_logic := '0';
    signal d1, q1, c1 : std_logic := '0';
    signal d3, q3 : std_logic_vector(2 downto 0) := (others => '0');
    signal d7, q7, c7 : std_logic_vector(6 downto 0) := (others => '0');
    signal d8, q8 : std_logic_vector(7 downto 0) := (others => '0');
    signal d9, q9 : std_logic_vector(8 downto 0) := (others => '0');
    signal d17, q17, c17 : std_logic_vector(16 downto 0) := (others => '0');
    signal d33, q33 : std_logic_vector(32 downto 0) := (others => '0');
    signal d64, q64 : std_logic_vector(63 downto 0) := (others => '0');
    signal d65, q65, c65 : std_logic_vector(64 downto 0) := (others => '0');
    signal d130, q130, c130 : std_logic_vector(129 downto 0) := (others => '0');
    signal dup, qup : std_logic_vector(0 to 11) := (others => '0');
    signal dbv, qbv, cbv : bit_vector(12 downto 0) := (others => '0');
    signal darr, qarr, carr : arr_t := (others => (others => '0'));

    constant NINE_VALUES : std_logic_vector(0 to 8) := "UX01ZWLH-";

    -- the value a Mode A island sees for a std_logic
    function lossy_of (x : std_logic) return std_logic is
    begin
        case x is
            when '1' | 'H' => return '1';
            when others    => return '0';
        end case;
    end function;

    function lossy_of (x : std_logic_vector) return std_logic_vector is
        variable r : std_logic_vector(x'range);
    begin
        for i in x'range loop
            r(i) := lossy_of(x(i));
        end loop;
        return r;
    end function;

    function expect (x : std_logic) return std_logic is
    begin
        if LOSSY then return lossy_of(x); else return x; end if;
    end function;

    function expect (x : std_logic_vector) return std_logic_vector is
    begin
        if LOSSY then return lossy_of(x); else return x; end if;
    end function;

    function expect_not (x : std_logic) return std_logic is
        variable r : std_logic := not x;
    begin
        if LOSSY then return not lossy_of(x); else return r; end if;
    end function;

    function expect_not (x : std_logic_vector) return std_logic_vector is
        variable r : std_logic_vector(x'range);
    begin
        for i in x'range loop
            r(i) := expect_not(x(i));
        end loop;
        return r;
    end function;

    -- pseudo-random pattern, a different one for each (width, step)
    function pat (n : natural; k : natural; s : natural)
        return std_logic_vector is
        variable r : std_logic_vector(0 to n - 1);
        variable x : natural := (k * 7919 + s * 104729 + n * 31) mod 65521 + 1;
    begin
        for i in r'range loop
            x := (x * 75 + 74) mod 65537;
            if NINE then
                r(i) := NINE_VALUES(x mod 9);
            elsif (x / 7) mod 2 = 0 then
                r(i) := '0';
            else
                r(i) := '1';
            end if;
        end loop;
        return r;
    end function;

    function bv_of (x : std_logic_vector) return bit_vector is
        variable r : bit_vector(x'range);
    begin
        for i in x'range loop
            if x(i) = '1' then r(i) := '1'; else r(i) := '0'; end if;
        end loop;
        return r;
    end function;

    function hex (x : std_logic_vector) return string is
        variable s : string(1 to x'length);
        variable j : natural := 0;
    begin
        for i in x'range loop
            j := j + 1;
            s(j) := std_logic'image(x(i))(2);
        end loop;
        return s;
    end function;

    function hex (x : bit_vector) return string is
        variable s : string(1 to x'length);
        variable j : natural := 0;
    begin
        for i in x'range loop
            j := j + 1;
            s(j) := bit'image(x(i))(2);
        end loop;
        return s;
    end function;
begin
    dut : entity work.edge_dut
        port map (clk, d1, d3, d7, d8, d9, d17, d33, d64, d65, d130, dup, dbv,
                  darr, q1, q3, q7, q8, q9, q17, q33, q64, q65, q130, qup, qbv,
                  qarr, c1, c7, c17, c65, c130, cbv, carr);

    stim : process is
        variable errors : natural := 0;

        procedure check (name : string; got, want : std_logic_vector) is
        begin
            if got /= want then
                report "MISMATCH " & name & " got " & hex(got) & " want "
                    & hex(want) severity error;
                errors := errors + 1;
            end if;
        end procedure;

        procedure check (name : string; got, want : bit_vector) is
        begin
            if got /= want then
                report "MISMATCH " & name & " got " & hex(got) & " want "
                    & hex(want) severity error;
                errors := errors + 1;
            end if;
        end procedure;

        variable p1 : std_logic_vector(0 to 0);
        variable pa : std_logic_vector(0 to 17);
    begin
        for k in 0 to 39 loop
            p1 := pat(1, k, 1);
            d1 <= p1(0);
            d3 <= pat(3, k, 2);
            d7 <= pat(7, k, 3);
            d8 <= pat(8, k, 4);
            d9 <= pat(9, k, 5);
            d17 <= pat(17, k, 6);
            d33 <= pat(33, k, 7);
            d64 <= pat(64, k, 8);
            d65 <= pat(65, k, 9);
            d130 <= pat(130, k, 10);
            dup <= pat(12, k, 11);
            dbv <= bv_of(pat(13, k, 12));
            pa := pat(18, k, 13);
            for i in 0 to 2 loop
                darr(i) <= pa(6 * i to 6 * i + 5);
            end loop;
            wait for 1 ns;
            clk <= '1'; wait for 1 ns; clk <= '0'; wait for 1 ns;

            -- registered values
            check("q1", (0 => q1), expect(p1));
            check("q3", q3, expect(pat(3, k, 2)));
            check("q7", q7, expect(pat(7, k, 3)));
            check("q8", q8, expect(pat(8, k, 4)));
            check("q9", q9, expect(pat(9, k, 5)));
            check("q17", q17, expect(pat(17, k, 6)));
            check("q33", q33, expect(pat(33, k, 7)));
            check("q64", q64, expect(pat(64, k, 8)));
            check("q65", q65, expect(pat(65, k, 9)));
            check("q130", q130, expect(pat(130, k, 10)));
            check("qup", qup, expect(pat(12, k, 11)));
            check("qbv", qbv, bv_of(pat(13, k, 12)));
            for i in 0 to 2 loop
                check("qarr", qarr(i), expect(pa(6 * i to 6 * i + 5)));
            end loop;

            -- combinational values
            check("c1", (0 => c1), expect_not(p1));
            check("c7", c7, expect_not(pat(7, k, 3)));
            check("c17", c17, expect_not(pat(17, k, 6)));
            check("c65", c65, expect_not(pat(65, k, 9)));
            check("c130", c130, expect_not(pat(130, k, 10)));
            check("cbv", cbv, not bv_of(pat(13, k, 12)));
            for i in 0 to 2 loop
                check("carr", carr(i), expect_not(pa(6 * i to 6 * i + 5)));
            end loop;

            report integer'image(k) & " " & hex(q130) & " " & hex(q65) & " "
                & hex(c130) & " " & hex(qarr(1)) & " " & hex(c17) & " "
                & hex(qbv);
        end loop;
        report "errors " & integer'image(errors);
        assert errors = 0 report "FAILED" severity failure;
        wait;
    end process;
end architecture;
VHDL_EOF

nvc --std=2008 --work=w_off -a edge_a.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e edge_tb -r 2>&1 | grep -v '^\*\* Debug' | sort > off.log
nvc --std=2008 --work=w_off --rtl-islands=off -e -gNINE=true edge_tb -r 2>&1 | grep -v '^\*\* Debug' | sort > off9.log

nvc --std=2008 --work=w_b -a edge_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_b --rtl-islands=hier -e edge_tb >b_dump.log
nvc --std=2008 --work=w_b -r edge_tb 2>&1 | grep -v '^\*\* Debug' | sort > b.log
nvc --std=2008 --work=w_b --rtl-islands=hier -e -gNINE=true edge_tb
nvc --std=2008 --work=w_b -r edge_tb 2>&1 | grep -v '^\*\* Debug' | sort > b9.log

nvc --std=2008 --work=w_a -a edge_a.vhd
NVC_DUMP_ISLANDS=1 nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e edge_tb >a_dump.log
nvc --std=2008 --work=w_a -r edge_tb 2>&1 | grep -v '^\*\* Debug' | sort > a.log
nvc --std=2008 --work=w_a --rtl-islands=hier --2state=islands -e -gNINE=true -gLOSSY=true edge_tb
nvc --std=2008 --work=w_a -r edge_tb 2>&1 | grep -v '^\*\* Debug' | sort > a9.log

cat a_dump.log
! grep -q "MISMATCH\|FAILED" off.log b.log a.log off9.log b9.log a9.log
grep -q "errors 0" a9.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log b.log
sed -i '/^\*\* Debug: /d' *.log   # debug builds only: JIT code footprint etc.
diff -u off.log a.log
diff -u off9.log b9.log

# Mode A is really used: the DUT island has two-state signals
grep -q 'EDGE_TB.DUT islands=1 .* two_state=[1-9]' a_dump.log
! grep -q 'two_state=[1-9]' b_dump.log
