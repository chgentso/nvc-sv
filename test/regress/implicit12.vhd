-- Implicit signals whose prefix is itself an implicit signal (LRM 14.7.4):
-- in the cycle of an event on s, s'STABLE is active and updates first, so
-- both s'STABLE'STABLE and s'STABLE'QUIET are false in that delta cycle.

entity implicit12 is
end entity;

architecture test of implicit12 is
    signal s : bit := '0';
begin
    main: process is
    begin
        wait for 1 ns;
        s <= '1';
        wait on s;
        assert not s'stable;
        assert not s'stable'stable;
        assert not s'stable'quiet;
        wait for 1 ns;
        assert s'stable;
        assert s'stable'stable;
        assert s'stable'quiet;
        wait;
    end process;
end architecture;
