package pu is
    generic (type ua is array (natural range <>) of integer);
end package;

package pc is
    generic (type ix is (<>); type ca is array (ix) of integer);
end package;

entity garray is
end entity;

architecture test of garray is
    subtype small is integer range 0 to 3;
    type c4 is array (0 to 3) of integer;
    type u1 is array (natural range <>) of integer;
    package ok_u is new work.pu generic map (ua => u1);           -- OK
    package ok_c is new work.pc generic map (ix => small, ca => c4); -- OK
    package bad_c is new work.pc generic map (ix => small, ca => u1); -- Error
begin
end architecture;
