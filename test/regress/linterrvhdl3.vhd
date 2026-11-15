library ieee; use ieee.std_logic_1164.all;
entity e3 is port (clk, a, b : in std_logic; q, t : out std_logic); end entity;
architecture rtl of e3 is
  signal s : std_logic;
begin
  p0: process (a) begin
    t <= a and ;
  end process;
  p1: process (a) begin
    if a = '1' then t <= b; end if;
  end process;
end architecture;
