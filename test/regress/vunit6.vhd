-- Instantiating an unbound PSL verification unit inside a bound one: the
-- items of the instantiated unit are elaborated in the context of the
-- instantiating unit's binding.  Two instances give two independent
-- copies, each producing its own cover hit.
-- IEEE 1850-2010 7.2.2, 9.2.

entity vunit6 is
end entity;

architecture test of vunit6 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

end architecture;

vunit vu_lib {
    -- Explicitly clocked so as not to depend on the default clock of
    -- the instantiating unit
    c1: cover {n = 2; n = 3} @ rising_edge(clk)
        report "vu_lib instance hit";                            -- 35 ns
    a1: assert (always (n <= 9)) @ rising_edge(clk)
        report "vu_lib a1 failed";
}

vunit vu_main (vunit6) {
    default clock is rising_edge(clk);

    i1 : vunit vu_lib;
    i2 : vunit vu_lib;

    c2: cover {n = 5} report "vu_main hit";                      -- 55 ns
}
