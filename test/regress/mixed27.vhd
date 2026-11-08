library ieee;
use ieee.std_logic_1164.all;

entity mixed27_e is
    port (a : inout std_logic;          -- Driven by this entity
          b : inout std_logic := 'Z';   -- No driver in this entity
          c : inout std_logic;
          a_rise, b_rise : out integer := 0);
end entity;

architecture test of mixed27_e is
begin

    a <= 'Z';
    c <= 'H';

    process (a) is
        variable n : integer := 0;
    begin
        if rising_edge(a) then
            n := n + 1;
        end if;
        a_rise <= n;
    end process;

    process (b) is
        variable n : integer := 0;
    begin
        if rising_edge(b) then
            n := n + 1;
        end if;
        b_rise <= n;
    end process;

end architecture;
