entity subprog4 is
end entity;

architecture test of subprog4 is
    type inner_a is record
        x : integer;
    end record;

    type inner_b is record
        y : integer;
    end record;

    type outer_a is record
        sub : inner_a;
        f0  : boolean;
    end record;

    type outer_b is record
        sub : inner_b;
        f1  : boolean;
    end record;

    function mk(v : integer := 1) return outer_a is
    begin
        return (sub => (x => v), f0 => v > 0);
    end function;

    function mk(v : integer := 2) return outer_b is
    begin
        return (sub => (y => v * 10), f1 => v > 0);
    end function;

    function mk(v : integer := 3) return boolean is
    begin
        return v > 0;
    end function;

    constant c1 : integer := mk.sub.x;
    constant c2 : integer := mk.sub.y;
    constant c3 : integer := mk(5).sub.x;
    constant c4 : integer := mk(6).sub.y;
begin
    check : process is
    begin
        assert c1 = 1;
        assert c2 = 20;
        assert c3 = 5;
        assert c4 = 60;
        assert mk(1).f0;
        assert mk(1).f1;
        assert mk(1);
        wait;
    end process;
end architecture;
