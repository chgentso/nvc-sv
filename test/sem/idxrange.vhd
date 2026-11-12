entity idxrange is
end entity;

architecture test of idxrange is
    type t1 is array (character range 1 to 8) of integer;  -- Error
    type t2 is array (character range 'a' to 'c') of integer;  -- OK
    type t3 is array (natural range 1 to 3) of integer;  -- OK
    type t4 is array (boolean range 0 to 1) of integer;  -- Error
begin
end architecture;
