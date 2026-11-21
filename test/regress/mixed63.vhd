library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package mixed63_pkg is
  type s16_arr_t is array (natural range <>) of signed(15 downto 0);
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.mixed63_pkg.all;

entity mixed63_sub is
  port (
    din  : in  s16_arr_t(0 to 3);
    dout : out s16_arr_t(0 to 3)
  );
end entity;

architecture rtl of mixed63_sub is
begin
  gen : for i in 0 to 3 generate
    dout(i) <= din(3 - i) + to_signed(i + 1, 16);
  end generate;
end architecture;
