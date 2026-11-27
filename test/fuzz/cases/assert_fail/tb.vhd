library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fz_tb is
end entity;

architecture sim of fz_tb is
    signal clk : std_logic := '0';
    signal o0  : unsigned(3 downto 0);
begin
    dut : entity work.m0 port map (clk => clk, o0 => o0);

    clk <= not clk after 5 ns;
end architecture;
