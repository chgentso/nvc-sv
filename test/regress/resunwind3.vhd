-- Nested resolution function triggered inside a running process (via force)
-- must preserve the outer caller's unwind target and frame anchor.
entity resunwind3 is
end entity;

architecture test of resunwind3 is
    type int_vec is array (natural range <>) of integer;

    function resolve_sum(v : int_vec) return integer is
        variable acc : integer := 0;
    begin
        for i in v'range loop
            acc := acc + v(i);
        end loop;
        return acc;
    end function;

    signal s : resolve_sum integer := 0;
begin
    p1: process is
    begin
        s <= 1;
        wait for 5 ns;
        s <= force 42;
        wait for 0 ns;
        assert s = 42;
        s <= release;
        -- Calling finish right after a nested resolution call must unwind cleanly
        std.env.finish;
    end process;

    p2: process is
    begin
        s <= 2;
        wait for 10 ns;
        assert false report "UNEXPECTED at 10 ns" severity failure;
        wait;
    end process;
end architecture;
