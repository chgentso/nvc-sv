package gaccess_pkg is
    generic (type d; type acc is access d);
end package;

-------------------------------------------------------------------------------

use std.textio.all;

entity gaccess is
end entity;

architecture test of gaccess is
    type int_ptr is access integer;
    package p1 is new work.gaccess_pkg generic map (d => integer, acc => int_ptr);  -- OK
    package p2 is new work.gaccess_pkg generic map (d => integer, acc => line);     -- Error
begin
end architecture;
