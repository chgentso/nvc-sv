library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity slice10 is
end entity;

architecture test of slice10 is
    function g return unsigned is
    begin
        return to_unsigned(1029, 16);
    end function;
begin
    process is
    begin
        assert to_integer(g(7 downto 0)) = 5;
        assert to_integer(g(15 downto 8)) = 4;
        wait;
    end process;
end architecture;
