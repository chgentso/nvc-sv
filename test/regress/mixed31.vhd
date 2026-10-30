-- Direct entity instantiation of a Verilog module
library ieee;
use ieee.std_logic_1164.all;

entity mixed31 is
end entity;

architecture test of mixed31 is
   signal clk : std_logic := '0';
   signal d, q : std_logic_vector(7 downto 0) := x"10";
begin
   u : entity work.mixed31_v
      generic map ( K => 2 )
      port map ( clk => clk, d => d, q => q );

   process is
   begin
      wait for 1 ns;
      assert q = x"12" report "FAILED" severity failure;
      report "PASSED";
      wait;
   end process;
end architecture;
