library ieee; use ieee.std_logic_1164.all;
entity vhderr4 is
  port (a, b : in std_logic; q : out std_logic;
        c : in nosuch_type);
end entity;
architecture rtl of vhderr4 is
begin
  p1: process (a) begin
    q <= a and b;
  end process;
  p2: process (a) begin
    q <= a and c;
  end process;
end architecture;
