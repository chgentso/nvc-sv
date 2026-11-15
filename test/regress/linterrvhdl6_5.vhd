library ieee
use ieee.numeric_std.all;

package bit_math is
  function width_for(count : positive) return natural;
  function is_pow2(count : positive) return boolean;
end package;

package body bit_math is
  function width_for(count : positive) return natural is
    variable w : natural := 0;
  begin
    while 2 ** w < count loop
      w := w + 1;
    end loop;
    return w;
  end;

  function is_pow2(count : positive) return boolean is
  begin
    return 2 ** width_for(count) = count;
  end;
end package body;
