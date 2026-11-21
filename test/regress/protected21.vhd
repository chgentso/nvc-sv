-- An incomplete type in a package completed by a protected type declaration
-- and designated by an access type in between (IEEE 1076-2019 5.4.2).

package protected21_pkg is
    type cell;
    type cell_ptr is access cell;
    type cell is protected
        procedure put(v : integer);
        impure function get return integer;
    end protected;
end package;

package body protected21_pkg is
    type cell is protected body
        variable val : integer := 0;
        procedure put(v : integer) is
        begin
            val := v;
        end procedure;
        impure function get return integer is
        begin
            return val;
        end function;
    end protected body;
end package body;

use work.protected21_pkg.all;

entity protected21 is
end entity;

architecture test of protected21 is
begin
    check: process is
        variable p : cell_ptr := new cell;
    begin
        p.put(42);
        assert p.get = 42;
        deallocate(p);
        wait;
    end process;
end architecture;
