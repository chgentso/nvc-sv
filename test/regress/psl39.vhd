-- IEEE 1850-2010 6.2.1.4.1: next_a[i to j](f) with an operand that is
-- not a Boolean: f must hold in each of the cycles i to j after the
-- current one.  All must hold; see psl52 for failing forms.

entity psl39 is
end entity;

architecture test of psl39 is
    signal clk         : bit := '0';
    signal a1, b1, c1  : bit;
    signal a2, b2, c2  : bit;
    signal a3, b3      : bit;
    signal a4, b4      : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0';
            a2 <= '0'; b2 <= '0'; c2 <= '0';
            a3 <= '0'; b3 <= '0';
            a4 <= '0'; b4 <= '0';
            case i is
                when 2 => a1 <= '1';
                when 3 => b1 <= '1';
                when 4 => b1 <= '1'; c1 <= '1';
                when 5 => c1 <= '1';
                when 7 => a2 <= '1';
                when 8 => b2 <= '1';
                when 9 => b2 <= '1'; c2 <= '1';
                when 10 => c2 <= '1';
                when 11 => a3 <= '1';
                when 12 | 13 => b3 <= '1';
                when 14 => a4 <= '1';           -- Weak: may end early
                when 15 => b4 <= '1';
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

    h1: assert always (a1 -> next_a[1 to 2] (b1 -> next c1));
    h2: assert always (a2 -> next_a[1 to 2] {b2; c2});
    h3: assert always (a3 -> next_a![1 to 2] (b3));
    h4: assert always (a4 -> next_a[1 to 3] (b4));

end architecture;
