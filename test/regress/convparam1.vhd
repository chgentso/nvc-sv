entity convparam1 is
end entity;

architecture test of convparam1 is
    type byte_vec is array (0 to 1) of integer range 0 to 255;
    type int_vec is array (0 to 1) of integer;

    function to_real (x : integer) return real is
    begin
        return real(x) * 2.0;
    end function;

    function to_int (x : real) return integer is
    begin
        return integer(x);
    end function;

    procedure set (a : integer; b : out integer) is
    begin
        b := a;
    end procedure;

    procedure add (a : integer; b : inout integer) is
    begin
        b := b + a;
    end procedure;

    procedure fill (x : out int_vec) is
    begin
        x := (1, 2);
    end procedure;

    procedure twice (x : inout int_vec) is
    begin
        x := (x(0) * 2, x(1) * 2);
    end procedure;
begin

    p1: process is
        variable r : real := 0.0;
        variable s : real := 10.0;
        variable bv : byte_vec := (5, 6);
    begin
        -- Conversion of an OUT formal
        set (21, to_real(b) => r);
        assert r = 42.0 report real'image(r);

        -- Conversions of both the formal and the actual for INOUT
        add (5, to_real(b) => to_int(s));
        assert s = 30.0 report real'image(s);

        -- Type conversions of array formals
        fill (byte_vec(x) => bv);
        assert bv = (1, 2);
        twice (byte_vec(x) => int_vec(bv));
        assert bv = (2, 4);
        wait;
    end process;

end architecture;
