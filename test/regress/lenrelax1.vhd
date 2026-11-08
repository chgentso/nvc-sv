-- A slice assignment between arrays of different length inside a function
-- that is never called: with --relaxed the length mismatch that is visible at
-- analysis time is tolerated, so the design analyses, elaborates and runs.
-- Expected behaviour: IEEE 1076-2008 (array assignment, 10.5.2.1 / 5.3.2.2:
-- the value must have the same number of elements as the target) would make
-- the assignment tmp(15 downto 0) := d erroneous if it were executed; the
-- function is not called, so nothing is executed and the process must reach
-- the end of its statements without error.
library ieee; use ieee.std_logic_1164.all;
entity lenrelax1 is end;
architecture a of lenrelax1 is
  function f (d : std_logic_vector(63 downto 0)) return std_logic_vector is
    variable tmp : std_logic_vector(31 downto 0);
  begin
    tmp(15 downto 0) := d;
    return tmp;
  end function;
begin
  process
    variable reached : boolean := false;
  begin
    reached := true;
    wait for 1 ns;
    assert reached report "process did not run" severity failure;
    wait;
  end process;
end;
