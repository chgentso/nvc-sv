-- A PSL verification unit bound to entity(architecture) is bound only
-- when that architecture is elaborated.  vu_unused names an architecture
-- that is not elaborated and must not be bound (its assertion would fail
-- at the first clock edge); vu_used must be bound.
-- IEEE 1850-2010 7.2.1; IEEE 1076-2008 13.1.

entity vunit2 is
end entity;

architecture unused of vunit2 is
    signal clk : bit := '0';
    signal n   : natural := 100;
begin
    clk <= not clk after 5 ns when now < 100 ns;
    n <= n + 1 when rising_edge(clk);
end architecture;

-- Analysed last so it is the default architecture of vunit2
architecture used of vunit2 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin
    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);
end architecture;

vunit vu_used (vunit2(used)) {
    default clock is rising_edge(clk);
    c1: cover {n = 1; n = 2} report "vu_used bound";     -- 25 ns
    a1: assert always (n < 10) report "vu_used a1 failed";
}

vunit vu_unused (vunit2(unused)) {
    default clock is rising_edge(clk);
    a1: assert always (n < 10) report "vu_unused must not be bound";
}
