-- The implicit exponentiation operator of a formal generic integer type
-- has an INTEGER right operand (IEEE 1076-2019 6.5.3, 9.2.8)

package genpack21_pkg is
    generic (type t is range <>);
    function square (x : t) return t;
    function power (x : t; n : natural) return t;
end package;

package body genpack21_pkg is
    function square (x : t) return t is
    begin
        return x ** 2;
    end function;

    function power (x : t; n : natural) return t is
    begin
        return x ** n;
    end function;
end package body;

-------------------------------------------------------------------------------

entity genpack21 is
end entity;

architecture test of genpack21 is
    type byte_t is range 0 to 255;
    package byte_pack is new work.genpack21_pkg generic map (t => byte_t);
    package int_pack is new work.genpack21_pkg generic map (t => integer);
begin

    process is
    begin
        assert byte_pack.square(5) = 25;
        assert byte_pack.power(2, 7) = 128;
        assert int_pack.power(-3, 3) = -27;
        wait;
    end process;

end architecture;
