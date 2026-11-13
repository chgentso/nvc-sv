library ieee; use ieee.std_logic_1164.all;
entity vhderr2 is
  port (a, b : in std_logic; q, l : out std_logic);
end entity;
architecture rtl of vhderr2 is
begin
  p1: process (a) begin
    q <= a and b;
  end process;
  p2: process (a, b) begin
    l <= a
    if b = '1' then l <= b; end if;
  end process;
  p3: process (a) begin
    q <= a and b;
  end process;
end architecture;
