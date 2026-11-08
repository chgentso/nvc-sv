-- PSL local variable declared at the start of a braced SERE and assigned
-- in a procedural block: the cover directive matches only when the value
-- captured at the start reappears on dout later in the same attempt.
-- IEEE 1850-2010 6.3, 6.4, 7.1.5; IEEE 1076-2008 11.1.

entity psl49 is
end entity;

architecture test of psl49 is
    signal clk  : bit := '0';
    signal n    : natural := 0;         -- Sampled value at edge k is k
    signal din  : integer;
    signal dout : integer;

    default clock is rising_edge(clk);
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);

    din <= 10 * n;
    dout <= 10 * (n - 2) when n >= 2 else -1;      -- din delayed two cycles

    -- Captures 30 at 35 ns, dout is 30 at 55 ns
    c1: cover {[[variable v : integer;]]
               (n = 3) [[v := din;]]; [*]; dout = v}
        report "c1 hit";

    -- Captures 40 at 45 ns but dout never equals v + 1 = 41
    c2: cover {[[variable v : integer;]]
               (n = 4) [[v := din + 1;]]; [*]; dout = v}
        report "c2 hit";

end architecture;
