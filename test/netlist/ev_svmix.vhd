library ieee;
use ieee.std_logic_1164.all;

entity ev_svmix is
end entity;

architecture test of ev_svmix is
    component ev_vtop is
        port ( clk_a, clk_b : in std_logic; d : in std_logic;
               q : out std_logic );
    end component;
    signal clk_a, clk_b, d, q : std_logic := '0';
begin
    u_top: ev_vtop port map ( clk_a => clk_a, clk_b => clk_b, d => d, q => q );
end architecture;
