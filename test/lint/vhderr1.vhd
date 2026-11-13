library ieee; use ieee.std_logic_1164.all;
entity vhderr1 is
  port (clk, a, b : in std_logic; q, l, m, n : out std_logic);
end entity;
architecture rtl of vhderr1 is
  signal s, t, unused_s : std_logic;
begin
  p1: process (a) begin
    q <= a and b;
  end process;
  p2: process (a, b) begin
    if a = '1' then l <= b; end if;
  end process;
  p3: process (clk) begin
    if rising_edge(clk) then
      s <= nosuch;
    end if;
  end process;
  p4: process (all) begin
    if a = '1' then m <= b; end if;
  end process;
  n <= a when b = '1';
  s <= a when rising_edge(clk);
  t <= nosuch2 and a;
end architecture;

library ieee; use ieee.std_logic_1164.all;
entity vhderr1b is
  port (a, b : in std_logic; q : out std_logic);
end entity;
architecture rtl of vhderr1b is
  signal unused_s : std_logic;
begin
  p1: process (a) begin
    q <= a and b;
  end process;
end architecture;
