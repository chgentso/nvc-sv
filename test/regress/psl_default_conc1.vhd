-- VHDL-2008 PSL declarations (default clock, property, sequence) in the
-- concurrent statement part of an architecture.
library ieee;
use ieee.std_logic_1164.all;

entity psl_default_conc1 is
end entity;

architecture test of psl_default_conc1 is
    signal clk : std_logic := '0';
    signal a   : std_logic := '0';
    signal b   : std_logic := '0';
begin
    default clock is rising_edge(clk);

    property p_req is always (a -> next b);
    p_check: assert p_req;

    stim: process is
    begin
        a <= '1';
        clk <= '0'; wait for 5 ns;
        clk <= '1'; wait for 5 ns;
        a <= '0'; b <= '1';
        clk <= '0'; wait for 5 ns;
        clk <= '1'; wait for 5 ns;
        b <= '0';
        clk <= '0'; wait for 5 ns;
        clk <= '1'; wait for 5 ns;
        wait;
    end process;
end architecture;
