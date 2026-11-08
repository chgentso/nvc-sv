-- The PSL directives assume, restrict, restrict!, fairness and strong fairness
-- embedded in comments (--psl) are accepted; all of them hold
-- on the simulated trace so the simulation must end without errors.
-- IEEE 1850-2010 7.1.2, 7.1.3, 7.1.4, 7.1.6.

entity psl47 is
end entity;

architecture test of psl47 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    -- psl default clock is rising_edge(clk);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    -- psl as1: assume always (n < 10);
    -- psl r1: restrict {n = 0; n = 1};
    -- psl r2: restrict! {n = 0; [*2]; n = 3};
    -- psl f1: fairness n = 3;
    -- psl f2: strong fairness n = 1, n = 2;

end architecture;
