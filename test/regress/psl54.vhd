-- IEEE 1850-2010 6.2.1.4.3 to 6.2.1.4.5: next_event, next_event_a and
-- next_event_e, failing forms.  The occurrences of the Boolean are
-- counted from the current cycle inclusive, and the strong form fails
-- at the end of the simulation if the Boolean has not occurred.

entity psl54 is
end entity;

architecture test of psl54 is
    signal clk                 : bit := '0';
    signal r1, t1, d1          : bit;
    signal r2, t2, d2          : bit;
    signal r3, t3, d3          : bit;
    signal r4, t4, d4          : bit;
    signal r5, t5, d5          : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 19 loop
            r1 <= '0'; t1 <= '0'; d1 <= '0';
            r2 <= '0'; t2 <= '0'; d2 <= '0';
            r3 <= '0'; t3 <= '0'; d3 <= '0';
            r4 <= '0'; t4 <= '0'; d4 <= '0';
            r5 <= '0'; t5 <= '0'; d5 <= '1';
            case i is
                when 2 => r1 <= '1'; t1 <= '1';
                when 4 => t1 <= '1'; d1 <= '1';
                when 5 => r2 <= '1'; t2 <= '1'; d2 <= '1';
                when 6 => d2 <= '1';
                when 7 => t2 <= '1';
                when 9 => r3 <= '1';
                when 10 => t3 <= '1'; d3 <= '1';
                when 12 => t3 <= '1';
                when 14 => r4 <= '1';
                when 15 | 16 => t4 <= '1';
                when 18 => r5 <= '1';
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

    -- Fails at 25 ns: t1 occurs in the current cycle without d1
    x1: assert always (r1 -> next_event(t1)(d1)) report "x1";
    -- Fails at 75 ns: second occurrence of t2 without d2
    x2: assert always (r2 -> next_event(t2)[2](d2)) report "x2";
    -- Fails at 125 ns: second occurrence of t3 without d3
    x3: assert always (r3 -> next_event_a(t3)[1 to 2](d3)) report "x3";
    -- Fails at 165 ns: no d4 at either occurrence of t4
    x4: assert always (r4 -> next_event_e(t4)[1 to 2](d4)) report "x4";
    -- Fails at the end: t5 never occurs
    x5: assert always (r5 -> next_event!(t5)(d5)) report "x5";

end architecture;
