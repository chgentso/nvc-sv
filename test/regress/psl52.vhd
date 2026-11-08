-- IEEE 1850-2010 6.2.1.4.1: next_a with an operand that is not a
-- Boolean, failing forms.  The strong form next_a! fails at the end of
-- the simulation if the last cycle of the range has not been reached.

entity psl52 is
end entity;

architecture test of psl52 is
    signal clk         : bit := '0';
    signal a1, b1, c1  : bit;
    signal a2, b2, c2  : bit;
    signal a3, b3      : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0';
            a2 <= '0'; b2 <= '0'; c2 <= '0';
            a3 <= '0'; b3 <= '1';
            case i is
                when 2 => a1 <= '1';
                when 3 => b1 <= '1';
                when 4 => b1 <= '1'; c1 <= '1';
                when 7 => a2 <= '1';
                when 8 => b2 <= '1';
                when 9 => b2 <= '1'; c2 <= '1';
                when 10 => c2 <= '0';
                when 14 => a3 <= '1';
                when others => null;
            end case;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Fails at 55 ns: c1 is not set in cycle 5
    h1: assert always (a1 -> next_a[1 to 2] (b1 -> next c1)) report "h1";

    -- Fails at 105 ns: the second {b2; c2} does not complete
    h2: assert always (a2 -> next_a[1 to 2] {b2; c2}) report "h2";

    -- Fails at the end: only one of the three cycles after cycle 14
    h3: assert always (a3 -> next_a![1 to 3] (b3)) report "h3";

end architecture;
