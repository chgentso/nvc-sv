-- IEEE 1850-2010 5.2.2: the PSL expressions Boolean -> Boolean and
-- Boolean <-> Boolean are Booleans, so they can appear as SERE elements
-- and as the Boolean operand of FL operators.

entity psl30 is
end entity;

architecture test of psl30 is
    signal clk            : bit := '0';
    signal a1, b1, c1     : bit;
    signal a2, b2, c2     : bit;
    signal a3, b3         : bit;
    signal a4, b4, c4     : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 23 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0';
            a2 <= '0'; b2 <= '0'; c2 <= '0';
            a3 <= '1'; b3 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '1';
            case i is
                -- (b1, c1) after a1 runs through the truth table
                when 1 | 3 | 5 | 7 => a1 <= '1';
                when 4 => c1 <= '1';
                when 6 => b1 <= '1';
                when 8 => b1 <= '1'; c1 <= '1';
                -- (b2, c2) after a2 runs through the truth table
                when 9 | 11 | 13 | 15 => a2 <= '1';
                when 12 => c2 <= '1';
                when 14 => b2 <= '1';
                when 16 => b2 <= '1'; c2 <= '1';
                -- a3 -> b3 holds only in cycles 18 and 19
                when 18 => b3 <= '1';
                when 19 => a3 <= '0';
                -- a4 -> b4 and a4 <-> b4 are false only in cycle 21
                when 21 => a4 <= '1';
                when 22 => c4 <= '0';
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

    -- Covered at 25, 45 and 85 ns
    i1: cover {a1; (b1 -> c1)} report "impl";
    -- Covered at 105 and 165 ns
    i2: cover {a2; (b2 <-> c2)} report "iff";
    -- Covered at 195 ns
    i3: cover {(a3 -> b3)[*2]} report "rep";

    -- Must hold: the left hand side is a Boolean (simple subset)
    h1: assert always (a4 -> b4) -> next c4;
    h2: assert always (a4 <-> b4) -> next c4;
    h3: assert always {(a4 -> b4)} |=> {c4};

end architecture;
