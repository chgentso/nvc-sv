-- PSL verification unit inheritance with vmode, vprop and vunit.  The
-- unbound vmode vm_base provides a default clock, a sequence declaration,
-- an auxiliary signal and an assumption; the bound vprop and vunit both
-- inherit it, so its names are visible in each of them and resolve in
-- the scope of the design they are bound to.  Each inheriting unit gets
-- its own copy of the inherited items.
-- IEEE 1850-2010 7.2, 7.2.3, 9.1, 9.2; IEEE 1076-2008 13.1.

entity vunit4 is
end entity;

architecture test of vunit4 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

end architecture;

vmode vm_base {
    signal next_n : natural;
    next_n <= n + 1;

    default clock is rising_edge(clk);

    sequence s_two is {n = 2};

    as1: assume always (n < 1000);
}

vprop vp_check (vunit4) {
    inherit vm_base;

    a1: assert always {s_two} |=> (n = 3) report "a1 failed";
    a2: assert always (next_n = n + 1) report "a2 failed";
}

vunit vu_cover (vunit4) {
    inherit vm_base;

    c1: cover {s_two; next_n = 4} report "inherited items visible";  -- 35 ns
}
