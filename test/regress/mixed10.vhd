library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity adder is
    port (
        a, b : in  std_logic_vector(3 downto 0);
        c    : in  std_logic;
        sum  : out std_logic_vector(4 downto 0);
        z    : out std_logic );
end entity;

architecture rtl of adder is
    signal s : unsigned(4 downto 0);
begin
    s   <= resize(unsigned(a), 5) + resize(unsigned(b), 5) + ("0000" & c);
    sum <= std_logic_vector(s);
    z   <= '1' when s = 0 else '0';
end architecture;
