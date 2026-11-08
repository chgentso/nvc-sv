-- IEEE 1850-2010 6.2.1.2: clocked FL properties that fail.  clk ticks
-- in every cycle, clk2 in even cycles.  The values of b1 and b2 in the
-- cycles where only clk ticks would make the properties hold if they
-- were evaluated on clk.

entity psl38 is
end entity;

architecture test of psl38 is
    signal clk, clk2   : bit := '0';
    signal a1, b1      : bit;
    signal a2, b2      : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            a1 <= '0'; b1 <= '0';
            a2 <= '0'; b2 <= '0';
            case i is
                when 4 => a1 <= '1';
                when 5 => b1 <= '1';
                when 9 => a2 <= '1';
                when 10 | 11 => b2 <= '1';
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

    -- Fails at 65 ns: b1 is not set in cycle 6
    g1: assert (always (a1 -> next b1)) @ rising_edge(clk2) report "g1";

    -- Fails at 125 ns: b2 is not set in cycle 12
    g2: assert always (a2 -> (next b2) @ rising_edge(clk2)) report "g2";

end architecture;
