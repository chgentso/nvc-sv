entity aggtarget2d is
end entity;

architecture test of aggtarget2d is
    type int_2d is array (natural range <>, natural range <>) of integer;
    type int_3d is array (natural range <>, natural range <>,
                          natural range <>) of integer;
begin
    p: process is
        variable m : int_2d(1 to 2, 1 to 3) := ((1, 2, 3), (4, 5, 6));
        variable c : int_3d(0 to 1, 0 to 1, 0 to 1) :=
            (((1, 2), (3, 4)), ((5, 6), (7, 8)));
        variable a, b, d, e, f, g : integer;
    begin
        ((a, b, d), (e, f, g)) := m;
        assert a = 1 and b = 2 and d = 3;
        assert e = 4 and f = 5 and g = 6;
        (((a, b), (d, e)), ((f, g), (m(1, 1), m(2, 2)))) := c;
        assert a = 1 and b = 2 and d = 3 and e = 4;
        assert f = 5 and g = 6 and m(1, 1) = 7 and m(2, 2) = 8;
        wait;
    end process;
end architecture;
