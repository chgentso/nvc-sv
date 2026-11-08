-- A PSL sequence declaration does not need to be lowered.
-- IEEE 1850-2010 6.5.1.

entity psl67 is
end entity;

architecture test of psl67 is
    signal clk : bit := '0';
    signal n   : natural := 0;

    -- psl default clock is rising_edge(clk);
    -- psl sequence s1 is {n = 1; n = 2};
    -- psl sequence s2 (boolean x) is {x; x};
begin

    clk <= not clk after 5 ns when now < 50 ns;
    n <= n + 1 when rising_edge(clk);

    check: process is
    begin
        wait for 60 ns;
        assert n = 5 report "unexpected count";
        report "PASSED";
        wait;
    end process;

end architecture;
