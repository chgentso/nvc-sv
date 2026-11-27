package subtype_prot1_pkg is
    type accum_t is protected
        procedure add(x : integer);
        impure function get return integer;
    end protected;
end package;

package body subtype_prot1_pkg is
    type accum_t is protected body
        variable total : integer := 0;
        procedure add(x : integer) is
        begin
            total := total + x;
        end procedure;
        impure function get return integer is
        begin
            return total;
        end function;
    end protected body;
end package body;

use work.subtype_prot1_pkg.all;
use std.textio.all;

entity subtype_prot1 is
end entity;

architecture test of subtype_prot1 is
    subtype accum_sub is accum_t;
    subtype text_sub is text;
    shared variable sv : accum_sub;
begin
    process is
        file f : text_sub;
    begin
        sv.add(11);
        sv.add(31);
        assert sv.get = 42;
        wait;
    end process;
end architecture;
