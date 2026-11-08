-- Case choice range whose bounds are function calls on slices (not locally
-- static; accepted with --relaxed).  Expected values: IEEE 1076-2008 10.9
-- (the alternative whose discrete range contains the expression is chosen),
-- numeric_std to_integer.
-- c_lo(18 downto 2) = x"7FF63"(18 downto 2) = 0x1FFD8 = 131032,
-- c_hi(18 downto 2) = x"7FF7B"(18 downto 2) = 0x1FFDE = 131038,
-- x = 131032 + 1 = 131033 is in the range, 131039 and 131031 are not.
library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity caserange1 is end;
architecture a of caserange1 is
  constant c_lo : unsigned(19 downto 0) := x"7FF63";
  constant c_hi : unsigned(19 downto 0) := x"7FF7B";
begin
  process
    variable x : integer := 0;
    variable hit : integer;
  begin
    assert to_integer("00" & c_lo(18 downto 2)) = 131032 report "low bound value" severity failure;
    assert to_integer("00" & c_hi(18 downto 2)) = 131038 report "high bound value" severity failure;
    for i in 0 to 4 loop
      case i is
        when 0 => x := to_integer("00" & c_lo(18 downto 2)) + 1;   -- 131033
        when 1 => x := 131032;                                      -- low bound
        when 2 => x := 131038;                                      -- high bound
        when 3 => x := 131031;
        when others => x := 131039;
      end case;
      hit := -1;
      case x is
        when to_integer("00" & c_lo(18 downto 2)) to to_integer("00" & c_hi(18 downto 2)) =>
          hit := 1;
        when others =>
          hit := 0;
      end case;
      if i <= 2 then
        assert hit = 1 report "x = " & integer'image(x) & " should be in range" severity failure;
      else
        assert hit = 0 report "x = " & integer'image(x) & " should be outside the range" severity failure;
      end if;
    end loop;
    wait;
  end process;
end;
