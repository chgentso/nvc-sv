-- PSL assume directives that hold in VHDL-2008 syntax, including an
-- assumption made through a declared property: simulation must end
-- without errors.
-- IEEE 1850-2010 7.1.2; IEEE 1076-2008 11.1 (PSL directives as
-- concurrent statements), 15.10 (assume is a reserved word).

entity psl44 is
end entity;

architecture test of psl44 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    default clock is rising_edge(clk);

    property p_bounded (const numeric lim) is always (n <= lim);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    as1: assume always (n >= 0);
    as2: assume p_bounded(9);
    as3: assume always {n = 2} |=> {n = 3; n = 4};
    assume never {n = 3; n = 5};            -- Unlabelled directive

end architecture;
