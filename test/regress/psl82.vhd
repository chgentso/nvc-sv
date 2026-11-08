-- IEEE 1850-2010 6.2.1.3.1: a strong sequence {r}! holds only if r
-- completes, so a match that is still pending at the end of the
-- simulation is a failure; the weak form {r} holds in that case.

entity psl82 is
end entity;

architecture test of psl82 is
    signal clk        : bit := '0';
    signal s1, b1, c1 : bit;
    signal s2, b2, c2 : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            s1 <= '0'; b1 <= '0'; c1 <= '0';
            s2 <= '0'; b2 <= '0'; c2 <= '0';
            case i is
                when 2 => s1 <= '1'; b1 <= '1';
                when 3 => c1 <= '1';
                when 5 => s2 <= '1'; b2 <= '1';
                when 9 => s1 <= '1'; b1 <= '1';
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

    -- The attempt in cycle 9 has not completed when the simulation ends
    a1: assert always (s1 -> {b1; c1}!) report "a1";

    -- c2 does not hold in cycle 6
    a2: assert always (s2 -> {b2; c2}!) report "a2";

    -- These hold
    g1: assert always (s1 -> {b1; c1}) report "g1";
    g2: assert always (s2 -> {b2}!) report "g2";
    g3: assert always (s1 -> next_e[1 to 2] (c1 or s1)) report "g3";

end architecture;
