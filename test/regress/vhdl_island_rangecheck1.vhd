library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_rangecheck1 is
end entity;

architecture test of vhdl_island_rangecheck1 is
   signal clk, rst : std_logic := '0';
   signal i_s      : integer;
   signal u_s      : unsigned(7 downto 0);
begin
   stim : process
   begin
      rst <= '1';
      wait for 10 ns;
      clk <= '1';
      wait for 10 ns;
      wait;
   end process;

   ff : process(clk, rst)
   begin
      if rst = '1' then
         i_s <= 0;
      elsif rising_edge(clk) then
         i_s <= 3;
      end if;
   end process;

   -- i_s starts at integer'left (-2147483648), which must trigger a NATURAL
   -- subtype range check failure even when fused into an RTL island.
   u_s <= to_unsigned(i_s, 8);
end architecture;
