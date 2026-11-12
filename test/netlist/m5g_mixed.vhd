library ieee;
use ieee.std_logic_1164.all;

entity m5g_mixed is
end entity;

architecture test of m5g_mixed is
    component vp is
        generic ( W : integer := 4; K : integer := 1 );
        port ( clk : in std_logic;
               d   : in std_logic_vector(W - 1 downto 0);
               q   : out std_logic_vector(W - 1 downto 0) );
    end component;

    signal clk  : std_logic := '0';
    signal d, q : std_logic_vector(5 downto 0);
begin
    u_vp: vp generic map ( W => 6 ) port map ( clk => clk, d => d, q => q );
end architecture;
