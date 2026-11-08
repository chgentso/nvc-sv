library ieee;
use ieee.std_logic_1164.all;

package mixed30_pkg is
   subtype t_word is std_logic_vector(7 downto 0);
   type t_in is record
      valid : std_logic;
      data  : t_word;
   end record;
   type t_out is record
      data  : t_word;
      ready : std_logic;
   end record;
   type t_in_array is array (0 to 1) of t_in;
   type t_grid is array (0 to 1, 0 to 2) of t_out;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.mixed30_pkg.all;

entity mixed30_sub is
   port ( clk   : in  std_logic;
          i     : in  t_in_array;
          o     : out t_grid;
          ifo   : out t_out;
          unused: in  t_in );
end entity;

architecture rtl of mixed30_sub is
   signal irq : std_logic := '0';
begin
   process (clk) is
   begin
      if rising_edge(clk) then
         irq <= i(1).valid;
         if i(0).valid = '1' then
            o(1, 2).data <= i(0).data;
            o(1, 2).ready <= '1';
         end if;
         ifo.data <= i(1).data;
         ifo.ready <= i(1).valid;
      end if;
   end process;
end architecture;
