entity partial3 is
end entity;

architecture test of partial3 is
    type int_2d is array (natural range <>, natural range <>) of integer;
    subtype m2 is int_2d(1 to 2, 1 to 2);

    procedure update (x : inout m2) is
    begin
        x(1, 1) := x(2, 2) * 10 + x(1, 2);
    end procedure;
begin
    p: process is
        variable v : m2 := ((1, 2), (3, 4));
    begin
        -- Each element of a multi-dimensional formal is associated with
        -- a different actual
        update(x(1, 1) => v(2, 2), x(1, 2) => v(2, 1),
               x(2, 1) => v(1, 2), x(2, 2) => v(1, 1));
        assert v = ((1, 2), (3, 13));
        wait;
    end process;
end architecture;
