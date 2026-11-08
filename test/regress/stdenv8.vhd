-- PslIsAssertCovered and SetPslCoverAssert in STD.ENV: an assert
-- directive is covered when an evaluation of its property completes
-- without failing, and with SetPslCoverAssert(TRUE) PslIsCovered also
-- requires every assert directive to be covered (IEEE 1076-2019 16.5;
-- LCS-2016-050).

entity stdenv8 is
end entity;

use std.env.all;

architecture test of stdenv8 is
    signal clk, a, b : bit := '0';

    default clock is rising_edge(clk);
begin

    clkgen: clk <= not clk after 5 ns when now < 80 ns;

    -- Rising edges at 5, 15, 25, ... 75 ns
    a <= '1' after 10 ns, '0' after 20 ns;
    b <= '1' after 20 ns, '0' after 30 ns;

    -- Covered at 25 ns
    c1: cover {a; b};

    -- Evaluations complete at every rising edge
    a1: assert always (a -> next b);

    check: process is
    begin
        assert not PslIsAssertCovered;
        assert not PslIsCovered;

        wait for 10 ns;                 -- 10 ns
        assert PslIsAssertCovered;
        assert not PslIsCovered;

        SetPslCoverAssert(TRUE);
        assert GetPslCoverAssert;
        assert not PslIsCovered;        -- C1 not yet covered

        wait for 20 ns;                 -- 30 ns
        assert PslIsCovered;            -- Both C1 and A1 covered
        assert PslIsAssertCovered;
        assert not PslAssertFailed;

        ClearPslState;
        assert not PslIsCovered;
        assert not PslIsAssertCovered;

        wait for 10 ns;                 -- 40 ns
        assert PslIsAssertCovered;
        assert not PslIsCovered;        -- C1 not covered again

        SetPslCoverAssert(FALSE);
        assert not PslIsCovered;

        wait;
    end process;

end architecture;
