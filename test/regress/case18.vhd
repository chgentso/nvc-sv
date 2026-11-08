-- Case choices that are conversions of function calls (not locally static):
-- accepted with --relaxed.  Expected values: IEEE 1076-2008 10.9 (case
-- statement: exactly the alternative whose choice equals the expression is
-- executed), 9.3.6 (type conversion of the closely related array types),
-- numeric_std to_unsigned (value 2 in 4 bits is "0010", 3 is "0011").
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity case18 is end;

architecture a of case18 is
  signal s : std_logic_vector(3 downto 0) := x"2";
  constant c : natural := 3;
begin
  process
    variable v : std_logic_vector(3 downto 0);
    variable hit : integer;
  begin
    hit := -1;
    case s is
      when std_logic_vector(to_unsigned(0, 4)) => hit := 0;
      when std_logic_vector(to_unsigned(2, 4)) => hit := 2;
      when others => hit := 99;
    end case;
    assert hit = 2 report "first case selected alternative " & integer'image(hit) & ", expected 2" severity failure;
    v := x"3";
    hit := -1;
    case v is
      when std_logic_vector(to_unsigned(c, 4)) => hit := 3;
      when others => hit := 99;
    end case;
    assert hit = 3 report "second case selected alternative " & integer'image(hit) & ", expected 3" severity failure;
    v := x"4";
    hit := -1;
    case v is
      when std_logic_vector(to_unsigned(c, 4)) => hit := 3;
      when others => hit := 99;
    end case;
    assert hit = 99 report "third case selected alternative " & integer'image(hit) & ", expected others (99)" severity failure;
    wait;
  end process;
end;
