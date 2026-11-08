library ieee;
use ieee.std_logic_1164.all;

package mixed52_pkg is
  type rec_t is record
    valid : std_logic;
    vec   : std_logic_vector(7 downto 0);
    num   : integer range 0 to 7;
  end record;
  type rec_arr_t is array (2 to 4) of rec_t;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.mixed52_pkg.all;

entity mixed52_e is
end entity;

architecture a of mixed52_e is
  signal r : rec_t := ('0', x"A5", 1);
  signal ra : rec_arr_t := (others => ('1', x"0F", 7));
begin
  process begin
    wait for 5 ns;
    ra(4).vec <= x"81";
    ra(3).num <= 2;
    r.num <= 6;
    wait for 5 ns;
    ra(4).valid <= '0';
    wait;
  end process;
end architecture;
