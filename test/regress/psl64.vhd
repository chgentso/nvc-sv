-- IEEE 1850-2010 6.2.3: replicated property with an index range.  In
-- forall v(0 to 1) in boolean : p the parameter is a vector of two
-- Booleans and p is replicated for each of its four values.

entity psl64 is
end entity;

architecture test of psl64 is
    signal clk          : bit := '0';
    signal pair, pair_d : boolean_vector(0 to 1);

    default clock is rising_edge(clk);
begin

    stim: process is
        type table_t is array (natural range <>) of boolean_vector(0 to 1);
        constant T : table_t := ((false, false), (false, true),
                                 (true, false), (true, true),
                                 (false, true), (true, true));
    begin
        for i in T'range loop
            pair <= T(i);
            if i > 0 then
                pair_d <= T(i - 1);
            end if;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Must hold: pair_d is pair delayed by one cycle
    r1: assert forall v(0 to 1) in boolean :
        always ((pair = v) -> next (pair_d = v));

end architecture;
