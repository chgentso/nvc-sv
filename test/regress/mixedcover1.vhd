-- VHDL component instantiated twice for a Verilog module.  Each instance
-- is one scope at its place in the hierarchy.

library ieee;
use ieee.std_logic_1164.all;

entity mixedcover1 is
end entity;

architecture test of mixedcover1 is
    component mixedcover1_sub is
        port ( a : in std_logic;
               y : out std_logic );
    end component;

    signal a1, a2, y1, y2 : std_logic := '0';
begin

    u1: component mixedcover1_sub port map (a1, y1);
    u2: component mixedcover1_sub port map (a2, y2);

    stim: process is
    begin
        a1 <= '1';
        wait for 1 ns;
        a2 <= '1';
        a1 <= '0';
        wait for 1 ns;
        a2 <= '0';
        wait for 1 ns;
        wait;
    end process;

end architecture;
