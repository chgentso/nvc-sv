library ieee; use ieee.std_logic_1164.all;
entity vhderr3 is
  port (a, b : in std_logic; q, l : out std_logic);
end entity;
architecture rtl of vhderr3 is
  signal bad : nosuch_type;
  signal ok : std_logic;
begin
  p1: process (a) begin
    q <= a and b;
    ok <= bad;
  end process;
  p2: process (a) begin
    l <= a and b;
  end process;
end architecture;
