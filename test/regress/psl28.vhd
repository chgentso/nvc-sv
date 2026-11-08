-- IEEE 1850-2010 5.2.3.6: ended() is true in each cycle in which its
-- sequence ends a match, with respect to the clock of the context or
-- to the clock given as second argument.  clk2 ticks in even cycles.

entity psl28 is
end entity;

architecture test of psl28 is
    signal clk, clk2 : bit := '0';
    signal ph        : boolean := false;    -- clk2 ticks in this cycle
    signal cnt       : natural;
    signal a, b, c   : bit;
    signal d, e      : bit;

    --                               0123456789
    constant SA : bit_vector := "0101100100";
    constant SB : bit_vector := "0010110010";
    constant SC : bit_vector := "0001011001";
    constant SD : bit_vector := "0010000000";
    constant SE : bit_vector := "0001100000";

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in SA'range loop
            a <= SA(i); b <= SB(i); c <= SC(i); d <= SD(i); e <= SE(i);
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

    -- Must hold: {a; b} ends at cycles 2, 4, 5 and 8, {a; b[*2]} at
    -- cycle 5, and {d; e} on clk2 at cycle 4 (on clk it ends at 3)
    a1: assert always cnt >= 1 ->
        (ended({a; b}) = (prev(a) = '1' and b = '1'));
    a2: assert always cnt >= 2 ->
        (ended({a; b[*2]}) = (prev(a, 2) = '1' and prev(b) = '1' and b = '1'));
    a3: assert always (ph and cnt >= 2) ->
        (ended({d; e}, rising_edge(clk2)) = (prev(d, 2) = '1' and e = '1'));
    a4: assert always ended({d; e}) -> (cnt = 3);

    -- Covered at 35, 55, 65 and 95 ns
    c1: cover {ended({a; b}); c} report "ended";

end architecture;
