library ieee;
use ieee.std_logic_1164.all;

package mixed23_pkg is
    function f (x : integer) return integer;
    constant C : string := "cc";
    constant R : real := 2.5;
end package;

package body mixed23_pkg is
    function f (x : integer) return integer is
    begin
        return x + 1;
    end function;
end package body;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.mixed23_pkg.all;

entity defgen is
    generic (W : integer := f(3);
             S : string  := C;
             G : real    := -1.5;
             T : real    := R;
             B : boolean := not false);
    port (d  : in std_logic_vector(W-1 downto 0);
          y  : out std_logic_vector(W-1 downto 0);
          ok : out std_logic);
end entity;

architecture test of defgen is
begin
    y <= d;
    ok <= '1' when S = "cc" and G = -1.5 and T = 2.5 and B else '0';
end architecture;
