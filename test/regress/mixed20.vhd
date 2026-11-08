library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed20 is
end entity;

architecture test of mixed20 is
    component mixed20_sub is
        port (a, b : in unsigned(3 downto 0);
              y    : out unsigned(3 downto 0);
              z    : out signed(3 downto 0));
    end component;

    signal a, b, y : unsigned(3 downto 0);
    signal z       : signed(3 downto 0);
begin
    u: component mixed20_sub port map (a, b, y, z);

    check: process is
    begin
        a <= x"5";
        b <= x"3";
        wait for 1 ns;
        assert y = x"8";
        assert z = x"2";
        a <= x"3";
        b <= x"5";
        wait for 1 ns;
        assert y = x"8";
        assert z = to_signed(-2, 4);
        wait;
    end process;
end architecture;
