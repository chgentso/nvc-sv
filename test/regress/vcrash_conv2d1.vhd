-- Conversion of a two dimensional array to an unconstrained type
library ieee;
use ieee.std_logic_1164.all;

entity vcrash_conv2d1 is
end entity;

architecture test of vcrash_conv2d1 is
    type arr_a is array (natural range <>, natural range <>) of std_logic;
    type arr_b is array (natural range <>, natural range <>) of std_logic;
    constant a : arr_a(0 to 0, 7 downto 0) := (others => (others => '0'));
    constant b : arr_b(0 to 0, 7 downto 0) := arr_b(a);
begin
    process is
    begin
        assert b'length(2) = 8;
        wait;
    end process;
end architecture;
