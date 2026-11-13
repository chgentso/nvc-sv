library ieee; use ieee.std_logic_1164.all;
entity sens1 is port (a, b, c, clk : in std_logic; q, r, s : out std_logic); end;
architecture test of sens1 is
  signal x, y : std_logic;
begin
  p1: process (a) is
  begin
    q <= a and b;
  end process;

  p2: process (all) is
  begin
    r <= a and b;
  end process;

  p3: process (clk) is
  begin
    if rising_edge(clk) then
      s <= a and b;
    end if;
  end process;

  p4: process (a,
               b) is
  begin
    x <= a and b and c and y;
    y <= a;
  end process;

  p5: process (a) is      -- nvc lint_off sensitivity
  begin
    x <= b;
  end process;
end;
