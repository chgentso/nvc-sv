-- LCS-2016-016: anonymous types on subprogram parameters.  A subprogram
-- with an anonymous parameter type is equivalent to an uninstantiated
-- subprogram whose implicit generic types are mapped from the subtypes of
-- the actuals at each call (IEEE 1076-2019 6.5.2, 6.5.7.1, 4.2.1).

package anon_pkg is
    procedure swap (x, y : inout type is private);
    function larger (a, b : type is <>) return boolean;
    function offset (e : type is (<>)) return natural;
    function twice (n : type is range <>) return integer;
    function total (v : type is array (natural range <>) of type is range <>)
        return integer;
    function count_of (v : type is array (natural range <>) of type is private;
                       x : v'element) return natural;
end package;

package body anon_pkg is
    procedure swap (x, y : inout type is private) is
        variable tmp : x'subtype;
    begin
        tmp := x;
        x := y;
        y := tmp;
    end procedure;

    function larger (a, b : type is <>) return boolean is
    begin
        return a > b;
    end function;

    function offset (e : type is (<>)) return natural is
        subtype e_t is e'subtype;
    begin
        return e_t'pos(e) - e_t'pos(e_t'low);
    end function;

    function twice (n : type is range <>) return integer is
    begin
        return integer(n + n);
    end function;

    function total (v : type is array (natural range <>) of type is range <>)
        return integer is
        variable sum : integer := 0;
    begin
        for i in v'range loop
            sum := sum + integer(v(i));
        end loop;
        return sum;
    end function;

    function count_of (v : type is array (natural range <>) of type is private;
                       x : v'element) return natural is
        variable n : natural := 0;
    begin
        for i in v'range loop
            if v(i) = x then
                n := n + 1;
            end if;
        end loop;
        return n;
    end function;
end package body;

-------------------------------------------------------------------------------

entity anontype2 is
end entity;

use work.anon_pkg.all;

architecture test of anontype2 is
    type color_t is (red, green, blue);
    type nat_vec is array (natural range <>) of natural;
    subtype byte_t is integer range 0 to 255;
    type byte_vec is array (natural range <>) of byte_t;
begin

    process is
        variable i1, i2   : integer;
        variable s1, s2   : string(1 to 3);
        variable bv1, bv2 : bit_vector(3 downto 0);
        variable c1, c2   : color_t;
        variable r1, r2   : real;
        variable b        : byte_t;
        variable nv       : nat_vec(1 to 4) := (1, 2, 3, 4);
        variable byv      : byte_vec(0 to 2) := (100, 150, 5);
    begin
        -- Same procedure called with different actual types
        i1 := 1; i2 := 2;
        swap(i1, i2);
        assert i1 = 2 and i2 = 1;

        s1 := "abc"; s2 := "xyz";
        swap(s1, s2);
        assert s1 = "xyz" and s2 = "abc";

        bv1 := "0011"; bv2 := "1100";
        swap(bv1, bv2);
        assert bv1 = "1100" and bv2 = "0011";

        c1 := red; c2 := blue;
        swap(c1, c2);
        assert c1 = blue and c2 = red;

        r1 := 1.5; r2 := -2.5;
        swap(r1, r2);
        assert r1 = -2.5 and r2 = 1.5;

        -- Scalar anonymous type
        assert larger(i1, i2);
        assert not larger(c2, c1);
        assert larger(r2, r1);
        assert larger(character'('b'), character'('a'));

        -- Discrete anonymous type
        assert offset(green) = 1;
        assert offset(c1) = 2;
        assert offset(character'('A')) = 65;
        assert offset(true) = 1;

        -- Integer anonymous type
        b := 100;
        assert twice(b) = 200;
        assert twice(i1) = 4;

        -- Array anonymous type with anonymous element type
        assert total(nv) = 10;
        assert total(byv) = 255;
        assert total(nv(2 to 3)) = 5;

        -- Later parameter whose subtype refers to an anonymous type
        assert count_of(nv, nv(2)) = 1;
        assert count_of(string'("abcab"), character'('b')) = 2;
        assert count_of(s1(1 to 0), character'('x')) = 0;

        wait;
    end process;

end architecture;
