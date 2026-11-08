-- PSL sequence declarations and sequence instantiation in VHDL-2008.
-- Covers formal parameters of every class allowed for a sequence:
-- PSL type classes boolean, bit, numeric and const numeric, HDL type
-- class (hdltype) and sequence parameters, plus an instance used as the
-- actual of another instance.
-- IEEE 1850-2010 6.5.1.1, 6.5.1.2, 6.5.2.1, 6.5.3.1;
-- IEEE 1076-2008 3.3.2 (PSL declarations in a block declarative part).

entity psl40 is
end entity;

architecture test of psl40 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
    signal d   : bit;

    default clock is rising_edge(clk);

    sequence s0 is {n = 0; n = 1};
    sequence s1 (boolean x, y) is {x; y};
    sequence s2 (const numeric k) is {n = 2; [*k]; n = 8};
    sequence s3 (hdltype natural v) is {n = v};
    sequence s4 (sequence s) is {s; n = 5};
    sequence s5 (bit x) is {x; x};
    sequence s6 (numeric m) is {n = m};
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);
    d <= '1' when n = 6 or n = 7 else '0';

    c0: cover s0 report "s0 hit";                  -- 15 ns
    c1: cover s1(n = 1, n = 2) report "s1 hit";    -- 25 ns
    c3: cover s3(3) report "s3 hit";               -- 35 ns
    c6: cover s6(2 * 2) report "s6 hit";           -- 45 ns
    c4: cover s4(s1(n = 3, n = 4)) report "s4 hit";  -- 55 ns
    c5: cover s5(d) report "s5 hit";               -- 75 ns
    c2: cover s2(5) report "s2 hit";               -- 85 ns

end architecture;
