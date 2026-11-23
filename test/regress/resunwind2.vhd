-- Fatal assertion inside a custom resolution function
entity resunwind2 is
end entity;

architecture test of resunwind2 is
    type int_vec is array (natural range <>) of integer;

    function resolve_fail(v : int_vec) return integer is
    begin
        if v'length >= 2 and v(v'left) = 1 and v(v'right) = 2 then
            assert false report "resolution failure" severity failure;
        end if;
        return v(v'left);
    end function;

    signal s : resolve_fail integer := 0;
begin
    p1: process is
    begin
        s <= 0;
        wait for 5 ns;
        s <= 1;
        wait for 5 ns;
        report "UNEXPECTED at 10 ns";
        wait;
    end process;

    p2: process is
    begin
        s <= 0;
        wait for 5 ns;
        s <= 2;
        wait;
    end process;
end architecture;
