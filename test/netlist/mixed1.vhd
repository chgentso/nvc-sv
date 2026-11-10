library ieee;
use ieee.std_logic_1164.all;

entity mixed1 is
end entity;

architecture test of mixed1 is
    component vsub is
        generic ( W : integer := 4 );
        port ( clk : in std_logic;
               d   : in std_logic_vector(W - 1 downto 0);
               q   : out std_logic_vector(W - 1 downto 0);
               io  : inout std_logic );
    end component;

    signal clk  : std_logic := '0';
    signal d, q : std_logic_vector(3 downto 0);
    signal x    : std_logic;
begin
    clk <= not clk after 5 ns;

    u_sub: vsub port map ( clk => clk, d => d, q => q, io => x );
end architecture;
