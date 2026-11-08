-- PSL declarations in an entity declarative part and PSL directives in
-- an entity statement part, using the entity's ports; the architecture
-- sees the entity's PSL declarations.
-- IEEE 1076-2008 3.2.3 (entity declarative items), 3.2.4 (entity
-- statements), 11.1; IEEE 1850-2010 6.5.2, 6.5.3.

entity psl51_sub is
    port (clk : in bit; n : in natural);

    default clock is rising_edge(clk);

    sequence s_step (const numeric k) is {n = k; n = k + 1};
    property p_mono is always (n < 1000);
begin
    e1: assert p_mono report "e1 failed";
    e2: cover s_step(2) report "entity cover hit";       -- 35 ns
end entity;

architecture test of psl51_sub is
begin
    a1: cover s_step(6) report "architecture cover hit"; -- 75 ns
end architecture;

-------------------------------------------------------------------------------

entity psl51 is
end entity;

architecture test of psl51 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    u: entity work.psl51_sub port map (clk, n);

end architecture;
