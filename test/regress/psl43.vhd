-- Parameterised PSL sequence and property declarations embedded in
-- comments (--psl), both in the declarative part and in the statement
-- part, with nested instances.
-- IEEE 1850-2010 6.5.2, 6.5.3.

entity psl43 is
end entity;

architecture test of psl43 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k

    -- psl default clock is rising_edge(clk);
    -- psl sequence s_pair (boolean x, y) is {x; y};
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    -- psl property p_follow (sequence s; boolean e) is
    --     always {s} |=> e;
    -- psl sequence s_three (const numeric base) is
    --     {s_pair(n = base, n = base + 1); n = base + 2};

    -- psl c1: cover s_three(4) report "s_three hit";           -- 65 ns
    -- psl a1: assert p_follow(s_pair(n = 1, n = 2), n = 3)
    --     report "a1 failed";
    -- psl a2: assert p_follow(s_three(6), n = 10)
    --     report "a2 failed";                                  -- Fails 95 ns

end architecture;
