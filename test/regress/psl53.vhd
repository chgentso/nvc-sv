-- IEEE 1850-2010 6.2.1.4.3 to 6.2.1.4.5: next_event, next_event_a and
-- next_event_e, weak and strong.  The occurrences of the Boolean are
-- counted from the current cycle inclusive.  All must hold; see
-- psl54 for the failing forms.

entity psl53 is
end entity;

architecture test of psl53 is
    signal clk                 : bit := '0';
    signal r1, t1, d1          : bit;
    signal r2, t2, d2          : bit;
    signal r3, t3, d3          : bit;
    signal r4, t4, d4          : bit;
    signal r5, t5, d5          : bit;
    signal r6, t6, d6          : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 25 loop
            r1 <= '0'; t1 <= '0'; d1 <= '0';
            r2 <= '0'; t2 <= '0'; d2 <= '0';
            r3 <= '0'; t3 <= '0'; d3 <= '0';
            r4 <= '0'; t4 <= '0'; d4 <= '0';
            r5 <= '0'; t5 <= '0'; d5 <= '0';
            r6 <= '0'; t6 <= '0'; d6 <= '0';
            case i is
                when 2 => r1 <= '1'; t1 <= '1'; d1 <= '1';
                when 3 => r2 <= '1'; t2 <= '1';
                when 4 => t1 <= '1';
                when 6 => r1 <= '1'; t2 <= '1'; d2 <= '1';
                when 7 => t2 <= '1';
                when 8 => t1 <= '1'; d1 <= '1';
                when 10 => r3 <= '1';
                when 11 => t3 <= '1'; d3 <= '1';
                when 13 => t3 <= '1'; d3 <= '1';
                when 14 => t3 <= '1';
                when 16 => r4 <= '1'; t4 <= '1';
                when 17 => t4 <= '1';
                when 19 => t4 <= '1'; d4 <= '1';
                when 21 => r5 <= '1';
                when 23 => t5 <= '1'; d5 <= '1';
                when 24 => r6 <= '1';
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

    e1: assert always (r1 -> next_event(t1)(d1));
    e2: assert always (r2 -> next_event(t2)[2](d2));
    e3: assert always (r3 -> next_event_a(t3)[1 to 2](d3));
    e4: assert always (r4 -> next_event_e(t4)[2 to 3](d4));
    e5: assert always (r5 -> next_event!(t5)(d5));
    e6: assert always (r6 -> next_event(t6)(d6));   -- Weak: t6 never set

end architecture;
