library ieee; use ieee.std_logic_1164.all;
entity vhderr5 is
  port (a, b : in std_logic; q : out std_logic);
end entity;
architecture rtl of vhderr5 is
begin
  p1: process (a, b) begin
    q <= a and b;
  end process;
end architecture;
