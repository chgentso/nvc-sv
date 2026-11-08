-- IEEE 1850-2010 6.1.2.5: clocked SERE.  In {r}@c the SERE r is
-- evaluated on the ticks of c, starting with the first tick of c at or
-- after the point where the clocked SERE starts; elsewhere the clock of
-- the context applies.  clk ticks in every cycle, clk2 in even cycles.

entity psl36 is
end entity;

architecture test of psl36 is
    signal clk, clk2   : bit := '0';
    signal a1, b1      : bit;
    signal a2, b2, c2  : bit;
    signal a3, b3      : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 21 loop
            a1 <= '0'; b1 <= '0';
            a2 <= '0'; b2 <= '0'; c2 <= '0';
            a3 <= '0'; b3 <= '0';
            case i is
                when 2 => a1 <= '1';
                when 4 => b1 <= '1';
                when 5 => a2 <= '1';
                when 6 => b2 <= '1';
                when 8 => c2 <= '1';
                when 10 => a2 <= '1';
                when 12 => b2 <= '1';
                when 14 => c2 <= '1';
                when 15 => a3 <= '1';
                when 16 => b3 <= '1';
                when 18 => a3 <= '1';
                when 20 => b3 <= '1';
                when others => null;
            end case;
            wait for 5 ns;
            clk <= '1';
            if i mod 2 = 0 then
                clk2 <= '1';
            end if;
            wait for 3 ns;
            clk  <= '0';
            clk2 <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Clock on the whole sequence: covered at 45 ns
    s1: cover {a1; b1} @ rising_edge(clk2) report "s1";

    -- Clock on a nested sequence: after a2, b2 is checked at the next
    -- tick of clk2.  Covered at 85 and 145 ns
    s2: cover {a2; {b2; c2} @ rising_edge(clk2)} report "s2";

    -- Must hold
    h1: assert always {a3} |=> ({b3} @ rising_edge(clk2));

end architecture;
