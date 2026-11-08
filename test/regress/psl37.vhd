-- IEEE 1850-2010 6.2.1.2: clocked FL properties.  An explicit clock on
-- the property overrides the default clock, and a clocked property
-- nested in another one is evaluated on its own clock: (next b)@c holds
-- if b holds at the second tick of c at or after the current cycle.
-- clk ticks in every cycle, clk2 in even cycles.  All must hold; see
-- psl38 for the failing forms.

entity psl37 is
end entity;

architecture test of psl37 is
    signal clk, clk2   : bit := '0';
    signal a1, b1      : bit;
    signal a2, b2      : bit;
    signal a3, b3      : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 19 loop
            a1 <= '0'; b1 <= '0';
            a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0';
            case i is
                when 4 => a1 <= '1';
                when 6 => b1 <= '1';
                when 5 => a2 <= '1';
                when 8 => b2 <= '1';
                when 10 => a2 <= '1';
                when 12 => b2 <= '1';
                when 15 => a3 <= '1';
                when 16 => b3 <= '1';
                when others => null;
            end case;
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

    -- a1 in cycle 4, b1 in cycle 6
    f1: assert (always (a1 -> next b1)) @ rising_edge(clk2);

    -- a2 in cycle 5 then b2 in cycle 8; a2 in cycle 10 then b2 in 12
    f2: assert always (a2 -> (next b2) @ rising_edge(clk2));

    -- Nested clock identical to the default clock
    f3: assert always ((a3 -> next b3) @ rising_edge(clk));

end architecture;
