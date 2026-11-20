library ieee;
use ieee.fixed_pkg.all;

entity fixed1 is
end entity;

architecture test of fixed1 is
begin
    p : process is
        variable u  : ufixed(1 downto -5);
        variable s  : sfixed(1 downto -5);
        variable s0 : sfixed(2 downto 0);
    begin
        -- Exact half rounds to even (0.015625 = 0.5 * 2^-5)
        u := to_ufixed(0.015625, 1, -5);
        assert u = to_ufixed(0.0, 1, -5);
        -- Slightly above half with guard bits "100" + nonzero sticky rounds up
        u := to_ufixed(0.0175, 1, -5);
        assert u = to_ufixed(0.03125, 1, -5);
        u := to_ufixed(0.019531249999, 1, -5);
        assert u = to_ufixed(0.03125, 1, -5);

        -- Same for sfixed positive and negative
        s := to_sfixed(0.015625, 1, -5);
        assert s = to_sfixed(0.0, 1, -5);
        s := to_sfixed(0.0175, 1, -5);
        assert s = to_sfixed(0.03125, 1, -5);
        s := to_sfixed(-0.015625, 1, -5);
        assert s = to_sfixed(0.0, 1, -5);
        s := to_sfixed(-0.0175, 1, -5);
        assert s = to_sfixed(-0.03125, 1, -5);

        -- Negative ties and sticky around integer boundaries
        s0 := to_sfixed(-0.5, 2, 0);
        assert s0 = to_sfixed(0.0, 2, 0);
        s0 := to_sfixed(-0.51, 2, 0);
        assert s0 = to_sfixed(-1.0, 2, 0);
        s0 := to_sfixed(-0.49, 2, 0);
        assert s0 = to_sfixed(0.0, 2, 0);
        s0 := to_sfixed(-1.5, 2, 0);
        assert s0 = to_sfixed(-2.0, 2, 0);
        s0 := to_sfixed(-1.51, 2, 0);
        assert s0 = to_sfixed(-2.0, 2, 0);
        s0 := to_sfixed(-1.49, 2, 0);
        assert s0 = to_sfixed(-1.0, 2, 0);
        wait;
    end process;
end architecture;
