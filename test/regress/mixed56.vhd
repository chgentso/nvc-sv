library ieee;
use ieee.std_logic_1164.all;

entity mixed56 is
end entity;

architecture test of mixed56 is
  component mixed56_v is
    port (a : in std_logic; b : in std_logic; o2 : out std_logic);
  end component;
  signal a, b, o2 : std_logic;
begin
  a <= '1';
  b <= '1';
  u: mixed56_v port map (a, b, o2);

  process is
  begin
    wait for 1 ns;
    assert o2 = '0' report "bad o2" severity failure;
    wait;
  end process;
end architecture;
