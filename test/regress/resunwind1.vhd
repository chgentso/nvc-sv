-- std.env.finish called from inside a custom resolution function
entity resunwind1 is
end entity;

architecture test of resunwind1 is
    type int_vec is array (natural range <>) of integer;

    function resolve_finish(v : int_vec) return integer is
    begin
        if v'length >= 2 and v(v'left) = 1 and v(v'right) = 2 then
            std.env.finish;
        end if;
        return v(v'left);
    end function;

    signal s : resolve_finish integer := 0;
begin
    p1: process is
    begin
        s <= 0;
        wait for 5 ns;
        s <= 1;
        wait for 5 ns;
        assert false report "UNEXPECTED at 10 ns" severity failure;
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
