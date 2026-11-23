-- Toggle coverage of a Verilog module under a VHDL component and of the
-- VHDL signals around it in one report.

library ieee;
use ieee.std_logic_1164.all;

entity mixedcover3 is
end entity;

architecture test of mixedcover3 is
    component mixedcover3_sub is
        port ( a : in std_logic;
               y : out std_logic_vector(1 downto 0) );
    end component;

    signal a1 : std_logic := '0';
    signal y1 : std_logic_vector(1 downto 0);
begin

    u1: component mixedcover3_sub port map (a1, y1);

    stim: process is
    begin
        wait for 1 ns;
        a1 <= '1';
        wait for 1 ns;
        a1 <= '0';
        wait for 1 ns;
        wait;
    end process;

end architecture;
