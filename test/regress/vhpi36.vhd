-- Shared variables of a package and of an architecture are visible through
-- VHPI.  The C side is test/vhpi/vhpi36.c.

package vhpi36_pkg is
    type inner_t is record
        x  : integer;
        ok : boolean;
    end record;

    type rec_t is record
        id    : integer;
        name  : string(1 to 4);
        inner : inner_t;
        r     : real;
    end record;

    type rec_array_t is array (0 to 1) of rec_t;

    shared variable info : rec_array_t;
    shared variable counter : integer;
    signal trig : integer := 0;
end package;

-------------------------------------------------------------------------------

entity vhpi36_sub is
end entity;

architecture test of vhpi36_sub is
    shared variable deep : integer;
begin
    process is
    begin
        deep := 77;
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity vhpi36 is
end entity;

use work.vhpi36_pkg.all;

architecture test of vhpi36 is
    type color_t is (red, green, blue);
    type vec_t is array (natural range <>) of integer;
    shared variable local : vec_t(1 to 3);
    shared variable col : color_t;
begin

    u: entity work.vhpi36_sub;

    p: process is
    begin
        info(0) := (3, "zero", (10, false), 0.5);
        info(1) := (7, "abcd", (42, true), 2.25);
        counter := 5;
        local := (11, 22, 33);
        col := green;
        trig <= 1;
        wait for 1 ns;
        info(1).name := "wxyz";
        info(1).inner.x := -8;
        info(0).inner.ok := true;
        counter := 6;
        local(2) := 99;
        col := blue;
        trig <= 2;
        wait;
    end process;

end architecture;
