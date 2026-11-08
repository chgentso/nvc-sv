-- A violated PSL assume directive is checked like an assertion in
-- simulation and reported at the cycle where the property fails.
-- IEEE 1850-2010 7.1.2; IEEE 1076-2008 11.1.

entity psl45 is
end entity;

architecture test of psl45 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    default clock is rising_edge(clk);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    as1: assume always (n /= 4);                    -- Fails at 45 ns
    as2: assume always (n = 6 -> next (n = 8));     -- Fails at 75 ns

end architecture;
