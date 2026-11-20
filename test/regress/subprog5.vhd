entity subprog5 is
end entity;

architecture test of subprog5 is
    type int_arr is array (natural range <>) of integer;

    function arr return int_arr is
    begin
        return (10, 20, 30);
    end function;

    function arr(n : integer) return int_arr is
    begin
        return (n, n + 1);
    end function;

    signal s : integer := 0;
begin
    s <= arr(1);

    check : process is
        variable v : int_arr(0 to 1);
    begin
        wait for 1 ns;
        assert s = 20;
        v := arr(7);
        assert v(0) = 7 and v(1) = 8;
        wait;
    end process;
end architecture;
