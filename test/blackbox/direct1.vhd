library ieee;
use ieee.std_logic_1164.all;

entity direct1 is
    port (clk_i : in std_logic;
          q_o   : out std_logic_vector(3 downto 0));
end entity;

architecture test of direct1 is
    signal x, y : std_logic_vector(3 downto 0);
    signal lock : std_logic;
begin
    -- The entities are not in any library: the ports are those that the
    -- port maps mention
    u_pll: entity prims.cellpll
        generic map (DIV => 4)
        port map (clk_in => clk_i, locked => lock, clk_out => open);

    u_buf: entity prims.cellbuf port map (a => x, b => y, en => '1');

    u_buf2: entity prims.cellbuf port map (x, y, '0');

    x <= (others => lock);
    q_o <= y;
end architecture;
