-- Package instance with a defaulted generic constant followed by a
-- generic subprogram that has a default
package vcrash_genmap1_util is
    function dbl (x : integer) return integer;
end package;

package body vcrash_genmap1_util is
    function dbl (x : integer) return integer is
    begin
        return 2 * x;
    end function;
end package body;

-------------------------------------------------------------------------------

use work.vcrash_genmap1_util.all;

package vcrash_genmap1_gp is
    generic ( n : integer := 3;
              function f (x : integer) return integer is dbl );
    constant k : integer := f(n);
end package;

-------------------------------------------------------------------------------

use work.vcrash_genmap1_gp;

entity vcrash_genmap1_e is
    generic ( package ip is new work.vcrash_genmap1_gp generic map (default) );
    port ( o : out integer );
end entity;

architecture test of vcrash_genmap1_e is
begin
    o <= ip.f(ip.k);
end architecture;

-------------------------------------------------------------------------------

entity vcrash_genmap1 is
end entity;

architecture test of vcrash_genmap1 is
    package i3 is new work.vcrash_genmap1_gp;
    signal s : integer;
begin
    u: entity work.vcrash_genmap1_e generic map ( ip => i3 )
        port map ( o => s );

    process is
    begin
        assert i3.k = 6;
        wait for 1 ns;
        assert s = 12 report integer'image(s);
        wait;
    end process;
end architecture;
