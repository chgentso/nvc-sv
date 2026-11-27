use std.env.all;

entity assert_only1 is
end entity;

architecture beh of assert_only1 is
begin
    p1: process is
    begin
        ClearVhdlAssert;
        assert GetVhdlAssertCount = 0 severity failure;

        -- Standalone report statements must not increment assert counts
        report "plain note" severity note;
        report "plain warning" severity warning;

        assert GetVhdlAssertCount = 0 severity failure;
        assert GetVhdlAssertCount(note) = 0 severity failure;
        assert GetVhdlAssertCount(warning) = 0 severity failure;
        assert not IsVhdlAssertFailed severity failure;
        assert not IsVhdlAssertFailed(warning) severity failure;

        -- Assert statements still increment counts when enabled
        assert false report "expected warning" severity warning;
        assert GetVhdlAssertCount(warning) = 1 severity failure;
        assert IsVhdlAssertFailed(warning) severity failure;

        -- Disabling asserts suppresses assert statements only
        SetVhdlAssertEnable(warning, false);
        assert false report "suppressed warning" severity warning;
        assert GetVhdlAssertCount(warning) = 1 severity failure;
        report "unsuppressed report" severity warning;
        assert GetVhdlAssertCount(warning) = 1 severity failure;

        SetVhdlAssertEnable(warning, true);
        ClearVhdlAssert;
        assert GetVhdlAssertCount = 0 severity failure;
        wait;
    end process;
end architecture;
