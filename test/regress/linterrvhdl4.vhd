library ieee; use ieee.std_logic_1164.all;
entity e4 is port (clk, a, b : in std_logic; q, t : out std_logic); end entity;
architecture rtl of e4 is
begin
  open_p : process begin
    wait;
  p1: process (a) begin
    if a = '1' then t <= b; end if;
  end process;
  p2: process (a) begin
    if a = '1' then q <= b; end if;
  end process;
end architecture;

junk tokens that are not a design unit ;

entity e4b is port (a, b : in std_logic; q : out std_logic); end entity;
architecture rtl of e4b is
begin
  p3: process (a) begin
    if a = '1' then q <= b; end if;
  end process;
end architecture;
