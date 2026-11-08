-- A PSL fairness statement is accepted and does not affect simulation.
-- IEEE 1850-2010 7.1.6.

entity psl66 is
end entity;

architecture test of psl66 is
    signal clk : bit := '0';
    signal n   : natural := 0;

    -- psl default clock is rising_edge(clk);
begin

    clk <= not clk after 5 ns when now < 50 ns;
    n <= n + 1 when rising_edge(clk);

    -- psl fairness n > 3;
    -- psl assert always (n < 100);

    check: process is
    begin
        wait for 60 ns;
        assert n = 5 report "unexpected count";
        report "PASSED";
        wait;
    end process;

end architecture;
