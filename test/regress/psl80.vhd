-- IEEE 1850-2010 6.2.1.4.1, 6.2.1.4.4, 6.2.1.6.1: every one of the
-- properties required by next_a[i to j] and next_event_a(b)[i to j] is
-- checked even when an earlier one holds in the current cycle, and a
-- Sequence used as a property holds if any of its matches succeeds.

entity psl80 is
end entity;

architecture test of psl80 is
    signal clk        : bit := '0';
    signal s1, b1, d1 : bit;
    signal s2, b2, c2 : bit;
    signal s3, b3, c3 : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            s1 <= '0'; b1 <= '0'; d1 <= '0';
            s2 <= '0'; b2 <= '0'; c2 <= '0';
            s3 <= '0'; b3 <= '0'; c3 <= '0';
            case i is
                when 1 => s1 <= '1'; b1 <= '1'; d1 <= '1';
                when 3 => b1 <= '1';
                when 4 => s2 <= '1'; b2 <= '1';
                when 5 => b2 <= '1'; c2 <= '1';
                when 7 => s3 <= '1'; b3 <= '1';
                when 8 => b3 <= '1';
                when 9 => c3 <= '1';
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

    -- b1 holds in cycles 1 and 3: d1 holds in cycle 1 but not in cycle 3
    a1: assert always s1 -> next_event_a(b1)[1 to 2](d1) report "a1";

    -- {b2; c2} holds from cycle 4 but not from cycle 5
    a2: assert always s2 -> next_a[0 to 1] {b2; c2} report "a2";

    -- These hold: b3 holds in cycles 7 and 8 and c3 in cycle 9 so only
    -- the longer match of {b3[*1 to 2]; c3} succeeds
    g1: assert always s3 -> {b3[*1 to 2]; c3} report "g1";
    g2: assert always s3 -> next_a[0 to 1] {b3[*1 to 2]; c3} report "g2";
    g3: assert always s1 -> next_event_a(b1)[1 to 1](d1) report "g3";

end architecture;
