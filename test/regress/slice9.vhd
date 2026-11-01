library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package slice9_pkg is
    constant c_addr : unsigned(19 downto 0) := x"7ffff";
end package;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.slice9_pkg.all;

entity slice9 is
end entity;

architecture test of slice9 is
    signal off : unsigned(18 downto 0) := "0011111111111111111";
begin
    process is
    begin
        case to_integer(off) is
            when to_integer("00" & c_addr(18 downto 2)) => null;
            when others => assert false;
        end case;
        off <= (others => '1');
        wait for 1 ns;
        case to_integer(off) is
            when to_integer("00" & c_addr(18 downto 2)) => assert false;
            when others => null;
        end case;
        wait;
    end process;
end architecture;
