-- Failing instances of parameterised PSL property declarations are
-- reported at the cycle the instantiated property fails, with the report
-- string of the directive when one is given.
-- IEEE 1850-2010 6.5.2.2, 6.5.3.2, 7.1.1; IEEE 1076-2008 11.1.

entity psl42 is
end entity;

architecture test of psl42 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    default clock is rising_edge(clk);

    property p_resp (boolean req; const numeric d; boolean ack) is
        always (req -> next[d] ack);
    property p_never (sequence s) is never {s};
    property p_ok (boolean req; const numeric d; boolean ack) is
        p_resp(req, d, ack);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    a1: assert p_resp(n = 2, 3, n = 4) report "resp failed";  -- Fails 55 ns
    a2: assert p_never({n = 6; n = 7});                       -- Fails 75 ns
    a3: assert p_ok(n = 2, 3, n = 5) report "a3 failed";       -- Holds

end architecture;
