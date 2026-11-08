entity ent1 is
    generic ( g1 : type is private );   -- OK
    port ( p1 : type is private );      -- OK
end entity;

entity ent2 is
    port (
        p1 : inout type is private bus;     -- Error
        p2 : in type is <> := 0;            -- Error
        p3 : in type is (<>);               -- OK
        p4 : in type is range <>;           -- OK
        p5 : in type is array (natural range <>) of type is private  -- OK
        );
end entity;

package pack is
    procedure swap (x, y : inout type is private);  -- OK
    function larger (a, b : type is <>) return boolean;  -- OK
    procedure proc1 (x : type is private := 5);  -- Error
end package;

use work.pack.all;

entity ent3 is
end entity;

architecture test of ent3 is
begin
    process is
        variable i, j : integer;
        variable b : bit;
    begin
        swap(i, j);                     -- OK
        swap(i, b);                     -- Error
        assert larger(i, j);            -- OK
        assert larger(b, '1');          -- OK
        wait;
    end process;
end architecture;
