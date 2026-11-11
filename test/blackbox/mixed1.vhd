library ieee;
use ieee.std_logic_1164.all;

entity mixed1 is
end entity;

architecture test of mixed1 is
    component cellbuf is
        port (i : in std_logic; o : out std_logic);
    end component;

    signal x, y, z : std_logic;
begin
    u1: cellbuf port map (i => x, o => y);
    u2: entity prims.cellpll port map (clk => y, q => z);
    u3: entity work.vsub port map (d => z, q => x);
end architecture;
