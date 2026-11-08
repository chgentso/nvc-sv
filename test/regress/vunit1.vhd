-- A PSL verification unit bound to a design entity by its
-- Hierarchical_HDL_Name.  The vunit is analysed into the work library
-- with the design and is bound when the entity is elaborated.  Its
-- items use the modeling layer (an auxiliary signal and a concurrent
-- assignment), a default clock declaration, a parameterised sequence
-- declaration, a cover and an assert directive, and names declared in
-- the bound architecture.
-- IEEE 1850-2010 7.2, 7.2.1, 8, 9.2; IEEE 1076-2008 13.1 (PSL
-- verification unit as a primary unit).

entity vunit1 is
end entity;

architecture test of vunit1 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

end architecture;

vunit vu1 (vunit1) {
    signal twice : natural;             -- Modeling layer
    twice <= 2 * n;

    default clock is rising_edge(clk);

    sequence s_twice (const numeric k) is {twice = k};

    c1: cover s_twice(6) report "twice is six";    -- 35 ns
    a1: assert always (twice = 2 * n) report "a1 failed";
    a2: assert always (n = 7 -> next (twice = 15)) report "a2 failed";  -- 85 ns
}
