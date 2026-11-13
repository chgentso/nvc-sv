entity recagg is
end entity;

architecture test of recagg is
    type rec is record
        s : string(1 to 3);
        n : positive;
        r : real;
    end record;

    constant c1 : rec := ("abc", 1, 1.0);       -- OK
    constant c2 : rec := ("abc", 0, 1.0);       -- Error
    constant c3 : rec := (n => 0, s => "abc", r => 2.0);  -- Error
    constant c4 : rec := (s => "abc", n => 5, r => 2.0);  -- OK
    subtype small is integer range 0 to 3;
    type rec2 is record
        x : small;
    end record;
    constant c5 : rec2 := (x => 4);             -- Error
begin
end architecture;
