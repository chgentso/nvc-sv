-- PSL verification units bound through block and generate statement
-- labels and component instances in a Hierarchical_HDL_Name, and by a
-- verification unit binding indication in a compound configuration
-- specification.  A unit bound through a for generate statement applies
-- to the instance in each iteration.
-- IEEE 1850-2010 7.2.1; IEEE 1076-2008 7.3.1, 7.3.4.

entity vunit10_sub is
    generic (G : natural);
    port (clk : in bit; n : in natural);
end entity;

architecture test of vunit10_sub is
    signal scaled : natural;
begin
    scaled <= n * G;
end architecture;

-------------------------------------------------------------------------------

vunit vu_spec {
    default clock is rising_edge(clk);
    a1: assert always (G = 5) report "vu_spec a1 failed";
    c1: cover {scaled = 30} report "bound by configuration specification";  -- 65 ns
}

-------------------------------------------------------------------------------

entity vunit10 is
end entity;

architecture test of vunit10 is
    component vunit10_sub is
        generic (G : natural);
        port (clk : in bit; n : in natural);
    end component;

    component vunit10_comp is
        generic (G : natural);
        port (clk : in bit; n : in natural);
    end component;

    for c1 : vunit10_comp use entity work.vunit10_sub(test);
        use vunit vu_spec;
    end for;

    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    b1: block is
    begin
        u1: entity work.vunit10_sub generic map (2) port map (clk, n);
    end block;

    g1: for i in 1 to 2 generate
        u2: entity work.vunit10_sub generic map (i * 10) port map (clk, n);
    end generate;

    g2: if true generate
        u3: component vunit10_sub generic map (3) port map (clk, n);
    end generate;

    c1: component vunit10_comp generic map (5) port map (clk, n);

end architecture;

-------------------------------------------------------------------------------

vunit vu_block (vunit10.b1.u1) {
    default clock is rising_edge(clk);
    a1: assert always (G = 2 and scaled = 2 * n) report "vu_block a1 failed";
    c1: cover {scaled = 6} report "bound through block b1";             -- 35 ns
}

vunit vu_gen (vunit10.g1.u2) {
    default clock is rising_edge(clk);
    a1: assert always (G = 10 or G = 20) report "vu_gen a1 failed";
    c1: cover {scaled = G * 4} report "bound through generate g1";      -- 45 ns
}

vunit vu_comp (vunit10.g2.u3) {
    default clock is rising_edge(clk);
    a1: assert always (G = 3) report "vu_comp a1 failed";
    c1: cover {scaled = 15} report "bound through component u3";        -- 55 ns
}
