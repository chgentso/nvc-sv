package alias16_pkg is
    type my_vec is array (natural range <>) of bit;
    subtype my_vec4 is my_vec(3 downto 0);

    -- User-defined "=" ignores MSB (bit 3)
    function "=" (l, r : my_vec) return boolean;
end package;

package body alias16_pkg is
    function "=" (l, r : my_vec) return boolean is
    begin
        return l(2) = r(2) and l(1) = r(1) and l(0) = r(0);
    end function;
end package body;

use work.alias16_pkg.all;

entity alias16 is
end entity;

architecture test of alias16 is
    constant a : my_vec4 := "0000";
    constant b : my_vec4 := "1000";
begin
    p1 : process is
        -- Makes predefined "=" for base type my_vec directly visible,
        -- hiding the potentially visible user-defined "=" from alias16_pkg.
        alias v4 is my_vec4;
    begin
        assert not (a = b);
        wait;
    end process;

    p2 : process is
    begin
        -- Only user-defined "=" is visible here.
        assert a = b;
        wait;
    end process;
end architecture;
