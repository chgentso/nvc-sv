-- A PSL verification unit bound to one instance through a hierarchical
-- path applies to that instance only.  u1 and u2 are instances of the
-- same entity with different generics: the assertion holds in u1 and
-- would fail in u2, and the cover shows the vunit is active in u1.
-- IEEE 1850-2010 7.2.1, 9.2; IEEE 1076-2008 13.1.

entity vunit3_sub is
    generic (G : natural);
    port (clk : in bit; n : in natural);
end entity;

architecture test of vunit3_sub is
    signal scaled : natural;
begin
    scaled <= G * n;
end architecture;

-------------------------------------------------------------------------------

entity vunit3 is
end entity;

architecture test of vunit3 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    u1: entity work.vunit3_sub generic map (1) port map (clk, n);
    u2: entity work.vunit3_sub generic map (2) port map (clk, n);

end architecture;

vunit vu_inst (vunit3.u1) {
    default clock is rising_edge(clk);
    a1: assert always (scaled = n) report "bound to wrong instance";
    c1: cover {G = 1 and scaled = 3} report "bound to u1";   -- 35 ns
}
