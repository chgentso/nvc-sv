-- IEEE 1850-2010 6.2.1.8: LTL operators, failing forms.  F and U are
-- strong and fail at the end of the simulation if their operand has
-- not held.

entity psl61 is
end entity;

architecture test of psl61 is
    signal clk                 : bit := '0';
    signal a1, b1              : bit;
    signal a2, b2              : bit;
    signal a3, b3              : bit;
    signal a4, b4, c4          : bit;
    signal a5, b5, c5          : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 19 loop
            a1 <= '0'; b1 <= '0';
            a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0';
            a4 <= '0'; b4 <= '0'; c4 <= '0';
            a5 <= '0'; b5 <= '0'; c5 <= '0';
            case i is
                when 2 => a1 <= '1';
                when 5 => a4 <= '1';
                when 6 => c4 <= '1';
                when 9 => a5 <= '1';
                when 10 => c5 <= '1';
                when 13 => a2 <= '1';
                when 16 => a3 <= '1';
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

    -- Fails at 35 ns
    q1: assert G (a1 -> X b1) report "q1";
    -- Fails at the end: b3 never set
    q2: assert G (a3 -> F b3) report "q2";
    -- Fails at 75 ns: c4 drops before b4 holds
    q3: assert G (a4 -> X [c4 U b4]) report "q3";
    -- Fails at 115 ns: c5 drops before b5 holds
    q4: assert G (a5 -> X [c5 W b5]) report "q4";
    -- Fails at 145 ns
    q5: assert G (a2 -> X! b2) report "q5";

end architecture;
