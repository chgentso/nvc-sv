-- IEEE 1850-2010 6.1.1.2.1: SERE or.  {r1} | {r2} matches whenever
-- either operand matches.

entity psl32 is
end entity;

architecture test of psl32 is
    signal clk                : bit := '0';
    signal a1, b1, c1         : bit;
    signal a2, b2, c2, d2     : bit;
    signal a4, b4, c4         : bit;
    signal a5, b5, c5         : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 22 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0';
            a2 <= '0'; b2 <= '0'; c2 <= '0'; d2 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '0';
            a5 <= '0'; b5 <= '0'; c5 <= '0';
            case i is
                when 1 => a1 <= '1';
                when 2 => b1 <= '1';
                when 4 => c1 <= '1';
                when 6 => a2 <= '1';
                when 7 => b2 <= '1';
                when 8 => d2 <= '1';
                when 10 => a2 <= '1';
                when 11 => c2 <= '1';
                when 12 => c2 <= '1';
                when 13 => d2 <= '1';
                when 15 => a4 <= '1';
                when 16 => c4 <= '1';
                when 17 => a4 <= '1';
                when 18 => b4 <= '1';
                when 19 => a5 <= '1';
                when 21 => c5 <= '1';
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

    o1: cover {{a1; b1} | {c1}} report "o1";               -- 25, 45 ns
    o2: cover {a2; {{b2} | {c2; c2}}; d2} report "o2";   -- 85, 135 ns
    o3: cover {{a5} | {b5} | {c5}} report "o3";            -- 195, 215 ns

    -- Must hold
    h1: assert always {a4} |=> {{b4} | {c4}};
    h2: assert always {a2} |=> {{b2; d2} | {c2; c2; d2}};

end architecture;
