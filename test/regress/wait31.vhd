entity wait31 is
end entity;

architecture test of wait31 is
    type int_arr_t is array (1 downto 0) of integer;
    type int_arr_ptr_t is access int_arr_t;

    signal s : integer := 1;
    signal r : integer := 0;
begin
    p1: process (all)
        variable p : int_arr_ptr_t;
    begin
        p := new int_arr_t'(s, 5);
        p.all(0) := p.all(1) + 1;
        r <= p(0);
        deallocate(p);
    end process;

    check: process is
    begin
        wait for 1 ns;
        assert r = 2 severity failure;
        s <= 10;
        wait for 1 ns;
        assert r = 11 severity failure;
        wait;
    end process;
end architecture;
