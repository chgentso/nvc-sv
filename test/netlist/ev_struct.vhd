library ieee;
use ieee.std_logic_1164.all;

entity ev_struct_v is
end entity;

architecture a of ev_struct_v is
  signal clk, clk2, d, y : std_logic;
  type f_t is record q : std_logic; qe : std_logic; end record;
  type s_t is record a : f_t; b : f_t; end record;
  signal r : s_t;
  signal z, w : std_logic;
begin
  process (clk) begin
    if rising_edge(clk) then r.a.q <= d; r.a.qe <= d; end if;
  end process;
  process (clk2) begin
    if rising_edge(clk2) then r.b.q <= d; r.b.qe <= d; end if;
  end process;
  z <= r.a.qe and r.a.q;
  process (clk) begin
    if rising_edge(clk) then w <= z; end if;
  end process;
  y <= w;
end architecture;
