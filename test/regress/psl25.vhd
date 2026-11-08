-- LCS-2016-043: predefined attributes of PSL directives.  D'SIGNAL is an
-- implicit BOOLEAN signal giving the value of the labelled directive;
-- D'EVENT is TRUE in the simulation cycle in which the directive
-- completes (IEEE 1076-2019 16.2).
--
-- Only the instants that are unambiguous are checked: both attributes are
-- FALSE before the directive has ever completed, 'SIGNAL is TRUE once the
-- cover directive has completed at 25 ns, and 'EVENT is TRUE in some delta
-- cycle of that time step and in no delta cycle of any other clock edge.

entity psl25 is
end entity;

architecture test of psl25 is
    signal clk, a, b : bit := '0';

    default clock is rising_edge(clk);
begin

    clkgen: clk <= not clk after 5 ns when now < 60 ns;

    -- Rising edges at 5, 15, 25, 35, 45, 55 ns
    a <= '1' after 10 ns, '0' after 20 ns;
    b <= '1' after 20 ns, '0' after 30 ns;

    -- Sequence {a; b} completes at the 25 ns edge only
    c1: cover {a; b};

    sig_check: process is
    begin
        assert not c1'signal;
        assert not c1'event;
        wait until c1'signal for 100 ns;
        assert c1'signal report "c1'signal never became TRUE";
        assert now = 25 ns
            report "c1'signal became TRUE at " & to_string(now);
        wait;
    end process;

    event_check: process is
        variable seen : boolean := false;
    begin
        for edge in 1 to 6 loop
            wait until clk = '1';
            -- Scan the delta cycles following the clock edge
            for delta in 1 to 4 loop
                if c1'event then
                    assert now = 25 ns
                        report "c1'event TRUE at " & to_string(now);
                    seen := true;
                end if;
                wait for 0 ns;
            end loop;
        end loop;
        assert seen report "c1'event never TRUE";
        wait;
    end process;

end architecture;
