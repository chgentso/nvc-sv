-- PSL local variables declared at the start of a parenthesised FL
-- property and assigned in a procedural block attached to a Boolean.
-- A new attempt starts every cycle and each attempt must see its own
-- copy of the local variable: the value captured at cycle k is compared
-- two cycles later, while later attempts have captured other values.
-- With a single shared copy the assertion would fail.  All assertions
-- hold, so the simulation must end without errors.
-- IEEE 1850-2010 6.3, 6.4; IEEE 1076-2008 11.1.

entity psl48 is
end entity;

architecture test of psl48 is
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

    -- Overlapping attempts, one per cycle
    a1: assert always ([[variable v : integer;]]
                       {true [[v := din;]]} |=> {true; dout = v})
        report "a1 failed";

    -- Local variable updated in several steps of one attempt
    a2: assert always ([[variable acc : integer;]]
                       {(n = 1) [[acc := din;]];
                        true [[acc := acc + din;]];
                        true [[acc := acc + din;]]} |-> (acc = 60))
        report "a2 failed";

end architecture;
