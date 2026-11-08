-- PSL restrict, restrict!, fairness and strong fairness directives in
-- VHDL-2008 syntax are accepted.  Every restriction here is satisfied by
-- the simulated trace, so the simulation must end without errors
-- whether or not an implementation checks restrictions.  Fairness
-- constraints concern infinite traces and have no observable effect in
-- a finite simulation.
-- IEEE 1850-2010 7.1.3, 7.1.4, 7.1.6; IEEE 1076-2008 11.1, 15.10.

entity psl46 is
end entity;

architecture test of psl46 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    default clock is rising_edge(clk);

    sequence s_init (const numeric k) is {n = 0; [*k]; n = k + 1};
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    r1: restrict {n = 0; n = 1; n = 2};
    r2: restrict! {n = 0; [*]; n = 5};
    r3: restrict s_init(2);
    restrict {(n < 10)[*]};                  -- Unlabelled directive
    f1: fairness n = 3;
    f2: strong fairness n = 1, n = 2;
    fairness (n mod 2 = 0);                 -- Unlabelled directive

end architecture;
