-- PSL interface subprograms of STD.ENV: PslIsCovered, PslAssertFailed,
-- ClearPslState, SetPslCoverAssert and GetPslCoverAssert
-- (IEEE 1076-2019 16.5; LCS-2016-050, LCS-2016-043).

entity stdenv7 is
end entity;

use std.env.all;

architecture test of stdenv7 is
    signal clk, a, b : bit := '0';

    default clock is rising_edge(clk);
begin

    clkgen: clk <= not clk after 5 ns when now < 80 ns;

    -- Rising edges at 5, 15, 25, ... 75 ns
    a <= '1' after 10 ns, '0' after 20 ns, '1' after 50 ns, '0' after 60 ns;
    b <= '1' after 20 ns, '0' after 30 ns, '1' after 60 ns, '0' after 70 ns;

    -- Covered at 25 ns and again at 65 ns
    c1: cover {a; b};

    -- Never fails
    a1: assert always (a -> next b);

    check: process is
    begin
        assert not PslIsCovered;
        assert not PslAssertFailed;

        SetPslCoverAssert(TRUE);
        assert GetPslCoverAssert;
        SetPslCoverAssert(FALSE);
        assert not GetPslCoverAssert;

        wait for 20 ns;
        assert not PslIsCovered;        -- Not yet covered

        wait for 10 ns;                 -- 30 ns
        assert PslIsCovered;
        assert not PslAssertFailed;

        ClearPslState;
        assert not PslIsCovered;        -- Coverage state cleared

        wait for 10 ns;                 -- 40 ns
        assert not PslIsCovered;

        wait for 30 ns;                 -- 70 ns
        assert PslIsCovered;            -- Covered again at 65 ns
        assert not PslAssertFailed;

        wait;
    end process;

end architecture;
