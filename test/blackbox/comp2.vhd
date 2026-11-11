library ieee;
use ieee.std_logic_1164.all;

entity comp2 is
end entity;

architecture test of comp2 is
    component cellbuf is
        generic (W : integer := 4);
        port (i : in std_logic_vector(W - 1 downto 0);
              o : out std_logic_vector(W - 1 downto 0));
    end component;

    -- This one has an entity
    component leaf is
        port (a : in std_logic; b : out std_logic);
    end component;

    signal x, y : std_logic_vector(3 downto 0);
    signal s, t : std_logic;
begin
    u1: cellbuf generic map (W => 4) port map (i => x, o => y);
    u2: cellbuf generic map (W => 4) port map (i => y, o => x);
    u3: leaf port map (a => s, b => t);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity leaf is
    port (a : in std_logic; b : out std_logic);
end entity;

architecture test of leaf is
begin
    b <= a;
end architecture;
