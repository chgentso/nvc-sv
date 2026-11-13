entity convparam is
end entity;

architecture test of convparam is
    function to_real (x : integer) return real is
    begin
        return real(x);
    end function;

    procedure set (a : integer; b : out integer) is
    begin
        b := a;
    end procedure;

    procedure get (a : in integer; b : inout integer) is
    begin
        b := b + a;
    end procedure;

    procedure sig (signal s : out integer) is
    begin
        s <= 1;
    end procedure;

    procedure rd (a : in integer) is
    begin
    end procedure;
begin
    process is
        variable r : real;
        variable i : integer;
    begin
        set (1, to_real(b) => r);       -- OK
        set (1, to_real(b) => i);       -- Error
        get (1, to_real(b) => r);       -- Error
        rd (to_real(a) => r);           -- Error
        wait;
    end process;
end architecture;
