entity protop1 is
end entity;

architecture test of protop1 is
    type acc is protected
        procedure set (v : integer);
        impure function "not" return integer;
        impure function "*" (k : integer) return integer;
    end protected;

    type acc is protected body
        variable n : integer := 0;

        procedure set (v : integer) is
        begin
            n := v;
        end procedure;

        impure function "not" return integer is
        begin
            return -n;
        end function;

        impure function "*" (k : integer) return integer is
        begin
            return n * k;
        end function;
    end protected body;

    shared variable sv : acc;
begin

    p1: process is
        variable a, b : integer;
    begin
        sv.set(5);
        a := sv."not";
        b := sv."*"(3);
        assert a = -5;
        assert b = 15;
        wait;
    end process;

end architecture;
