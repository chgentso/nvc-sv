library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity numeric_resize0 is
end entity;

architecture test of numeric_resize0 is
begin
    process is
        variable a : unsigned(3 downto 0) := "0011";
        variable s : signed(3 downto 0) := "1011";
        variable zu : unsigned(0 downto 1);
        variable zs : signed(0 downto 1);
    begin
        -- Resizing to zero gives the null array 0 downto 1
        zu := resize(a, 0);
        assert zu'length = 0 and zu'left = 0 and zu'right = 1;
        zs := resize(s, 0);
        assert zs'length = 0 and zs'left = 0 and zs'right = 1;
        assert resize(a, 6) = "000011";
        assert resize(s, 6) = "111011";
        wait;
    end process;
end architecture;
