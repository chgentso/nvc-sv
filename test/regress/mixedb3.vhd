library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
package mixedb3_pk is
  type t_arr is array(3 downto 0) of signed(7 downto 0);
end package;
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
use work.mixedb3_pk.all;
entity mixedb3_e is
  port (c : in t_arr; o : out std_logic_vector(7 downto 0));
end entity;
architecture a of mixedb3_e is
begin
  o <= std_logic_vector(c(0) + c(3));
end architecture;
