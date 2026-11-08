-- Binding PSL verification units without a Hierarchical_HDL_Name through
-- verification unit binding indications in a configuration declaration
-- (to the root design entity) and in a component configuration (to a
-- component instance).  vu_stray is not named in any binding indication
-- and must not be bound (its assertion would fail at the first edge).
-- IEEE 1076-2008 3.4.1, 3.4.3 (verification_unit_binding_indication),
-- 13.1; IEEE 1850-2010 7.2.1.

entity vunit5_sub is
    port (clk : in bit; n : in natural);
end entity;

architecture test of vunit5_sub is
    signal half : natural;
begin
    half <= n / 2;
end architecture;

-------------------------------------------------------------------------------

entity vunit5_top is
end entity;

architecture test of vunit5_top is
    component vunit5_sub is
        port (clk : in bit; n : in natural);
    end component;

    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    u1: component vunit5_sub port map (clk, n);

end architecture;

-------------------------------------------------------------------------------

vunit vu_top {
    default clock is rising_edge(clk);
    c1: cover {n = 4} report "vu_top bound by configuration";      -- 45 ns
}

vunit vu_sub {
    default clock is rising_edge(clk);
    c1: cover {n = 6 and half = 3} report "vu_sub bound by configuration";  -- 65 ns
}

vunit vu_stray {
    default clock is rising_edge(clk);
    a1: assert always false report "vu_stray must not be bound";
}

configuration vunit5 of vunit5_top is
    use vunit vu_top;
    for test
        for u1 : vunit5_sub
            use entity work.vunit5_sub(test);
            use vunit vu_sub;
        end for;
    end for;
end configuration;
