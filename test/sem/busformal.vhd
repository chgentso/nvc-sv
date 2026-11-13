package busformal is
    type t_vec is array (natural range <>) of bit;
    procedure p1 (signal s : out bit bus);     -- OK before 2008
    procedure p2 (signal s : inout t_vec bus); -- OK before 2008
end package;
