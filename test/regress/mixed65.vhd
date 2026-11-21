library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed65_dut is
  port (
    clk : in  std_logic;
    en  : in  std_logic;
    cnt : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of mixed65_dut is
  signal q : unsigned(7 downto 0) := (others => '0');
begin
  process(clk)
  begin
    if rising_edge(clk) then
      if en = '1' then
        q <= q + 1;
      end if;
    end if;
  end process;

  cnt <= std_logic_vector(q);
end architecture;
