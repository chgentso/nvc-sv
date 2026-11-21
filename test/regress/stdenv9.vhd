-- PslIsCovered returns FALSE when there are no PSL cover directives and
-- GetPslCoverAssert is FALSE (IEEE 1076-2019 16.5.8).

entity stdenv9 is
end entity;

use std.env.all;

architecture test of stdenv9 is
    signal clk, a, b : bit := '0';
    default clock is rising_edge(clk);
begin
    clk <= not clk after 5 ns when now < 30 ns;
    a1: assert always (a -> next b);

    check: process is
    begin
        assert not PslIsCovered;
        assert not PslIsAssertCovered;
        assert not PslAssertFailed;
        assert not GetPslCoverAssert;
        wait;
    end process;
end architecture;
