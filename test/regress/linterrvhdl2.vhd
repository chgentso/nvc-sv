library ieee; use ieee.std_logic_1164.all;
entity e2 is port (clk, a, b : in std_logic; q, t : out std_logic); end entity;
architecture rtl of e2 is
  signal broken : integer := ;
  signal s : std_logic;
begin
  p1: process (a) begin
    if a = '1' then t <= b; end if;
  end process;
  p2: process (clk) begin
    if rising_edge(clk) then q <= s; end if;
  end process;
end architecture;
