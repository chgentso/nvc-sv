library ieee; use ieee.std_logic_1164.all;
entity comp1 is end entity;
architecture a of comp1 is
  component cellbuf is
    generic (W : integer := 4);
    port (i : in std_logic_vector(W-1 downto 0); o : out std_logic_vector(W-1 downto 0));
  end component;
  signal x, y : std_logic_vector(3 downto 0);
begin
  u1: cellbuf generic map (W => 4) port map (i => x, o => y);
end architecture;
