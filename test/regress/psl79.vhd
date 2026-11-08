-- IEEE 1850-2010 6.2.1.4.3-5: the Boolean operand of next_event,
-- next_event_a and next_event_e can use the logical operators and and or.

entity psl79 is
end entity;

architecture test of psl79 is
    signal clk                 : bit := '0';
    signal a, b, c, d          : bit;
    signal s1, s2, s3          : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            a <= '0'; b <= '0'; c <= '0'; d <= '0';
            s1 <= '0'; s2 <= '0'; s3 <= '0';
            case i is
                when 0 => s1 <= '1';
                when 1 | 9 => a <= '1';
                when 3 | 7 => b <= '1';
                when 4 => a <= '1'; d <= '1'; s2 <= '1';
                when 5 => s3 <= '1';
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

    -- a or b holds in cycles 1, 3, 4, 7 and 9

    -- The second occurrence after cycle 0 is cycle 3 where c does not hold
    a1: assert always s1 -> next_event(a or b)[2](c) report "a1";

    -- The first two occurrences from cycle 4 are 4 and 7 but d does not
    -- hold in cycle 7
    a2: assert always s2 -> next_event_a(a or b)[1 to 2](d) report "a2";

    -- The first two occurrences from cycle 5 are 7 and 9 but a and c
    -- holds in neither
    a3: assert always s3 -> next_event_e(a or b)[1 to 2](a and c)
        report "a3";

    -- These hold
    g1: assert always s1 -> next_event(a or b)(a or b) report "g1";
    g2: assert always s3 -> next_event_e(a or b)[1 to 2](b) report "g2";
    g3: assert always s2 -> next_event_a(a and not b)[1 to 1](a)
        report "g3";

end architecture;
