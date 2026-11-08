library ieee;
use ieee.std_logic_1164.all;

package mixed38_pkg is
    function width (wide : boolean) return natural;
    function log2 (n : natural) return natural;
end package;

package body mixed38_pkg is
    function width (wide : boolean) return natural is
    begin
        if wide then
            return 16;
        else
            return 8;
        end if;
    end function;

    function log2 (n : natural) return natural is
        variable r : natural := 0;
    begin
        while 2**r < n loop
            r := r + 1;
        end loop;
        return r;
    end function;
end package body;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.mixed38_pkg.all;

entity mixed38_sub is
    generic (g_wide : boolean := false;
             g_depth : natural := 16);
    port (d : out std_logic_vector(width(g_wide) - 1 downto 0);
          a : out std_logic_vector(log2(g_depth) - 1 downto 0));
end entity;

architecture test of mixed38_sub is
begin
    d <= (others => '1');
    a <= (others => '1');
end architecture;
