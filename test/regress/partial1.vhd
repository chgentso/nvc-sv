entity partial1 is
end entity;

architecture test of partial1 is
    type row is array (0 to 1) of integer;
    type mat is array (0 to 2) of row;
    type pair is record
        x, y : integer;
    end record;
    type pairs is array (0 to 1) of pair;

    procedure rotate (m : inout mat) is
        variable t : row := m(0);
    begin
        m(0) := m(1);
        m(1) := m(2);
        m(2) := t;
    end procedure;

    procedure incr (p : inout pairs) is
    begin
        for i in p'range loop
            p(i).x := p(i).x + 1;
            p(i).y := p(i).y + 10;
        end loop;
    end procedure;
begin

    p1: process is
        variable a, b, c, d, e, f : integer;
    begin
        a := 1; b := 2; c := 3; d := 4; e := 5; f := 6;
        -- Nested partial association of an inout parameter
        rotate (m(0)(0) => a, m(0)(1) => b, m(1)(0) => c, m(1)(1) => d,
                m(2)(0) => e, m(2)(1) => f);
        assert a = 3 and b = 4 and c = 5 and d = 6 and e = 1 and f = 2
            report "rotate";

        -- Elements associated out of order
        a := 1; b := 2; c := 3; d := 4; e := 5; f := 6;
        rotate (m(2)(1) => f, m(2)(0) => e, m(1)(1) => d, m(1)(0) => c,
                m(0)(1) => b, m(0)(0) => a);
        assert a = 3 and b = 4 and c = 5 and d = 6 and e = 1 and f = 2
            report "rotate reversed";

        -- Fields of array elements
        a := 1; b := 2; c := 3; d := 4;
        incr (p(0).x => a, p(0).y => b, p(1).x => c, p(1).y => d);
        assert a = 2 and b = 12 and c = 4 and d = 14 report "incr";
        wait;
    end process;

end architecture;
