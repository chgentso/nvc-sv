-- IEEE 1850-2010 5.2.3.1, 5.2.3.3, 5.2.3.4, 5.2.3.5: prev(), stable(),
-- rose() and fell() with an explicit clock expression argument.  The
-- value is taken with respect to the given clock and not the clock of
-- the context.  clk ticks every cycle, clk2 only in even cycles, so at
-- an even tick the previous cycle of clk2 is two cycles of clk back.

entity psl26 is
end entity;

architecture test of psl26 is
    signal clk, clk2 : bit := '0';
    signal ph        : boolean := false;    -- clk2 ticks in this cycle
    signal cnt       : natural;
    signal s         : bit;

    constant SS : bit_vector(0 to 11) := "010110110110";

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in SS'range loop
            s   <= SS(i);
            cnt <= i;
            ph  <= (i mod 2 = 0);
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

    -- Must hold
    p1: assert always (ph and cnt >= 2) ->
        prev(cnt, 1, rising_edge(clk2)) = cnt - 2;
    p2: assert always (ph and cnt >= 4) ->
        prev(cnt, 2, rising_edge(clk2)) = cnt - 4;
    p3: assert always (cnt >= 1) -> prev(cnt) = cnt - 1;
    r1: assert always (ph and cnt >= 2) ->
        (rose(s, rising_edge(clk2)) = (s = '1' and prev(s, 2) = '0'));
    f1: assert always (ph and cnt >= 2) ->
        (fell(s, rising_edge(clk2)) = (s = '0' and prev(s, 2) = '1'));
    s1: assert always (ph and cnt >= 2) ->
        (stable(s, rising_edge(clk2)) = (s = prev(s, 2)));

    -- rose at 45 ns and 105 ns, fell at 85 ns, stable at 25 ns and 65 ns
    c1: cover {ph and cnt >= 2 and rose(s, rising_edge(clk2))}
        report "rose2";
    c2: cover {ph and cnt >= 2 and fell(s, rising_edge(clk2))}
        report "fell2";
    c3: cover {ph and cnt >= 2 and stable(s, rising_edge(clk2))}
        report "stable2";

end architecture;
