library ieee;
use ieee.std_logic_1164.all;

entity svnbamix1_dut is
  port (clk : in std_logic;
        a   : in std_logic;
        q   : out std_logic_vector(1 downto 0));
end entity;

architecture test of svnbamix1_dut is
  signal ad : std_logic;
begin
  ad <= a;

  process (clk) is
  begin
    if rising_edge(clk) then
      q <= a & ad;
    end if;
  end process;
end architecture;
