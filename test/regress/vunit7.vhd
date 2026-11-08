-- Overriding assignments in PSL verification units: a signal named in an
-- override specification is driven by the assignment in the vunit
-- instead of by its assignment in the design.  Two unrelated vunits
-- bound to the same design override two different signals.
-- IEEE 1850-2010 7.2.4, 7.2.4.1, 7.2.4.2.

entity vunit7 is
end entity;

architecture test of vunit7 is
    signal clk  : bit := '0';
    signal n    : natural := 0;         -- Sampled value at edge k is k
    signal mode : natural;
    signal en   : boolean;
    signal cnt  : natural := 0;
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    mode <= 0;                          -- Overridden by vu_mode
    en <= false;                        -- Overridden by vu_en

    -- Design logic observing the overridden signals
    cnt <= cnt + mode when rising_edge(clk) and en;

end architecture;

vunit vu_mode (vunit7) {
    override mode;
    mode <= 3;

    default clock is rising_edge(clk);
    a1: assert always (mode = 3) report "mode not overridden";
}

vunit vu_en (vunit7) {
    override en;
    en <= n >= 2;

    default clock is rising_edge(clk);
    a1: assert always (en = (n >= 2)) report "en not overridden";
    -- cnt increments by 3 at edges k >= 2
    c1: cover {n = 4 and cnt = 6} report "design sees overrides";  -- 45 ns
}
