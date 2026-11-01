library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity slice8 is
end entity;

architecture test of slice8 is
    function g return unsigned is
    begin
        return to_unsigned(1029, 16);
    end function;

    function h (x : integer) return unsigned is
    begin
        return to_unsigned(x, 16);
    end function;
begin
    process is
    begin
        assert to_integer(g(7 downto 0)) = 5;
        assert to_integer(g(15 downto 8)) = 4;
        assert to_integer(h(1029)(7 downto 0)) = 5;
        assert to_integer(h(258)) = 258;
        assert g(1) = '0';
        assert g(2) = '1';
        wait;
    end process;
end architecture;
