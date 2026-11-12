-- Alias of a protected type method that has parameters
package vcrash_protalias1_pkg is
    type counter is protected
        procedure add (n : integer);
        impure function get return integer;
    end protected;
end package;

package body vcrash_protalias1_pkg is
    type counter is protected body
        variable total : integer := 0;
        procedure add (n : integer) is
        begin
            total := total + n;
        end procedure;
        impure function get return integer is
        begin
            return total;
        end function;
    end protected body;
end package body;

-------------------------------------------------------------------------------

use work.vcrash_protalias1_pkg.all;

entity vcrash_protalias1 is
end entity;

architecture test of vcrash_protalias1 is
    shared variable sv : counter;
    alias add1 is sv.add [integer];
begin
    process is
    begin
        add1(2);
        add1(5);
        assert sv.get = 7;
        wait;
    end process;
end architecture;
