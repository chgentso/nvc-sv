-- Element names of array signals reached through VHPI carry the real
-- VHDL index.  The C side is test/vhpi/vhpi37.c.

entity vhpi37 is
end entity;

architecture test of vhpi37 is
    type int_vec is array (natural range <>) of integer;
    type int_mat is array (natural range <>, natural range <>) of integer;
    type neg_vec is array (integer range <>) of integer;

    signal up   : int_vec(16 to 19) := (100, 101, 102, 103);
    signal down : int_vec(47 downto 44) := (200, 201, 202, 203);
    signal neg  : neg_vec(-4 to -1) := (300, 301, 302, 303);
    signal mat  : int_mat(1 to 2, 5 downto 4) := ((10, 11), (12, 13));
begin
end architecture;
