library ieee;
use ieee.std_logic_1164.all;

package mixed61_pkg is
  type rec_t is record
    bias : integer range -128 to 127;
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.mixed61_pkg.all;

entity mixed61_e is
  port (
    clk : in  std_logic;
    o   : out std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of mixed61_e is
  signal cnt : integer range -128 to 127 := 10;
  signal r   : rec_t := (bias => -5);
  signal sum : signed(15 downto 0) := (others => '0');
begin
  process(clk)
  begin
    if rising_edge(clk) then
      sum <= to_signed(cnt + r.bias, 16);
      cnt <= cnt + 1;
    end if;
  end process;
  o <= std_logic_vector(sum);
end architecture;
