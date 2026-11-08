-- IEEE 1850-2010 6.1.2.1: r[*] matches zero or more consecutive
-- repetitions of r, so in a concatenation the next element starts in the
-- same cycle after zero repetitions and in the cycle after the last one
-- otherwise.

entity psl84 is
end entity;

architecture test of psl84 is
    signal clk        : bit := '0';
    signal a1, b1, c1 : bit;
    signal a2, c2     : bit;
    signal a3, b3     : bit;
    signal a4, c4     : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0'; a2 <= '0'; c2 <= '0';
            a3 <= '0'; b3 <= '0'; a4 <= '0'; c4 <= '0';
            case i is
                when 1 => a1 <= '1';
                when 2 => c1 <= '1';           -- No repetition of b1
                when 3 => a4 <= '1';
                when 4 => a1 <= '1';
                when 5 | 6 => b1 <= '1';
                when 7 => c1 <= '1';           -- Two repetitions of b1
                when 9 => a2 <= '1';
                when 11 => c2 <= '1';
                when 13 => a3 <= '1';
                when 15 => b3 <= '1';
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

    -- Matches end in cycles 2 and 7
    c1_c: cover {a1; b1[*]; c1} report "c1";

    -- Match ends in cycle 11
    c2_c: cover {a2; [*]; c2} report "c2";

    -- Match ends in cycle 15
    c3_c: cover {a3; [*]; b3} report "c3";

    -- c4[*] as the last element matches zero times in cycle 3
    c4_c: cover {a4; c4[*]} report "c4";

end architecture;
