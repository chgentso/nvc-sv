-- A PSL property declaration whose body contains HDL operators is
-- type checked when it is declared.  IEEE 1850-2010 6.5.2.

entity psl65 is
end entity;

architecture test of psl65 is
    signal clk : bit := '0';
    signal n   : natural := 0;

    -- psl default clock is rising_edge(clk);
    -- psl property p1 is always (n >= 0 and n < 100);
    -- psl property p2 (boolean x) is always (x or n = 0);
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
