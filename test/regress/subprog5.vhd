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

    type alert_t is (FAILURE, ERROR, WARNING);
    type alert_count_t is array (alert_t) of integer;

    function get_alert_count(id : integer := 0) return alert_count_t is
    begin
        return (FAILURE => id, ERROR => id + 1, WARNING => id + 2);
    end function;

    function get_alert_count(id : integer := 0) return integer is
    begin
        return id + 100;
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
        assert get_alert_count(5) = 105;
        assert get_alert_count(ERROR) = 1;
        wait;
    end process;
end architecture;
