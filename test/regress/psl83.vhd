-- IEEE 1850-2010 6.3: each evaluation of a property with local variables
-- needs its own copy of them.  When the number of evaluations in progress
-- cannot be bounded statically only a fixed number of copies is
-- available and running out of them stops the simulation.

entity psl83 is
end entity;

architecture test of psl83 is
    signal clk : bit := '0';
    signal n   : natural := 0;

    default clock is rising_edge(clk);
begin

    clk <= not clk after 5 ns when now < 400 ns;
    n <= n + 1 when rising_edge(clk);

    -- A new evaluation starts in every cycle and none ever completes
    c1: cover {[[variable v : natural;]] true [[v := n;]]; [*]; n < v};

end architecture;
