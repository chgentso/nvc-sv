entity subprog3 is
end entity;

architecture test of subprog3 is
    type my_int is range 0 to 1000;

    function to_my_int(x : integer) return my_int is
    begin
        return my_int(x);
    end function;

    procedure p_in(variable v : in my_int; expected : my_int) is
    begin
        assert v = expected;
    end procedure;

    procedure p_out(variable v : out my_int) is
    begin
        v := 42;
    end procedure;

    procedure p_inout(variable v : inout my_int) is
    begin
        v := v + 5;
    end procedure;
begin
    check : process is
        variable n : natural := 12;
    begin
        p_in(v => my_int(n), expected => 12);
        p_in(v => to_my_int(n), expected => 12);
        p_out(integer(v) => n);
        assert n = 42;
        p_inout(integer(v) => my_int(n));
        assert n = 47;
        wait;
    end process;
end architecture;
