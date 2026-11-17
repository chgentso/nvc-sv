library ieee;
use ieee.std_logic_1164.all;

entity mem2 is
   port (clk : in std_logic;
         we : in std_logic;
         addr : in natural range 0 to 65535;
         din : in std_logic_vector(7 downto 0);
         dout : out std_logic_vector(7 downto 0));
end entity;

architecture test of mem2 is
   type ram_t is array (0 to 65535) of std_logic_vector(7 downto 0);
   signal ram : ram_t;
   type small_t is array (0 to 3) of std_logic_vector(7 downto 0);
   signal small : small_t;
begin
   p1: process (clk) is
   begin
      if rising_edge(clk) then
         if we = '1' then
            ram(addr) <= din;
         end if;
         dout <= ram(addr);
         small(addr mod 4) <= din;
      end if;
   end process;
end architecture;
