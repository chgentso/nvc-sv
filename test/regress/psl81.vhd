-- IEEE 1850-2010 6.2.3: replicated property with an index range,
-- failing forms.  forall v(i to j) in S : p is replicated for every
-- combination of values from S for the elements of v.

entity psl81 is
end entity;

architecture test of psl81 is
    signal clk : bit := '0';
    signal x   : integer_vector(1 to 2);

    default clock is rising_edge(clk);
begin

    stim: process is
        type table_t is array (natural range <>) of integer_vector(1 to 2);
        constant T : table_t := ((5, 5), (0, 2), (3, 3), (4, 9), (1, 0));
    begin
        for i in T'range loop
            x <= T(i);
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Fails when both elements of x are in 0 to 2: cycles 1 and 4
    r1: assert forall v(1 to 2) in {0 to 2} : always (x /= v) report "r1";

    -- The sum of two values from {1, 3} is 2, 4 or 6: cycle 3
    r2: assert forall v(0 to 1) in {1, 3} : always (x(1) /= v(0) + v(1))
        report "r2";

end architecture;
