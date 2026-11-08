-- PSL property declarations and property instantiation in VHDL-2008.
-- All assertions hold, so the simulation must end without errors.
-- Covers formal parameters of class boolean, bitvector, string, const
-- numeric, hdltype, sequence and property, and a property declared in
-- terms of an instance of another property.
-- IEEE 1850-2010 6.5.1.1, 6.5.1.2, 6.5.2.2, 6.5.3.2;
-- IEEE 1076-2008 3.3.2, 11.1.

library ieee;
use ieee.numeric_bit.all;

entity psl41 is
end entity;

architecture test of psl41 is
    signal clk : bit := '0';
    signal n   : natural := 0;          -- Sampled value at edge k is k
    signal bv  : bit_vector(3 downto 0);
    signal tag : string(1 to 2) := "ok";

    default clock is rising_edge(clk);

    property p0 is always (n >= 0);
    property p1 (boolean x; const numeric k) is always (x -> next[k] (n = 5));
    property p2 (sequence s; boolean e) is always {s} |=> e;
    property p3 (property q) is q;
    property p4 (hdltype natural lim) is always (n <= lim);
    property p5 is p4(20);
    property p6 (bitvector v) is always (v /= "1111");
    property p7 (string s) is always (tag = s);
    property p8 (boolean x) is never x;
begin

    clk <= not clk after 5 ns when now < 100 ns;   -- Edges at 5, 15, .. 95 ns
    n <= n + 1 when rising_edge(clk);
    bv <= bit_vector(to_unsigned(n mod 16, 4));

    a0: assert p0;
    a1: assert p1(n = 3, 2) report "a1 failed";
    a2: assert p2({n = 1; n = 2}, n = 3) report "a2 failed";
    a3: assert p3(always (n < 100)) report "a3 failed";
    a4: assert p4(9) report "a4 failed";
    a5: assert p5 report "a5 failed";
    a6: assert p6(bv) report "a6 failed";
    a7: assert p7("ok") report "a7 failed";
    a8: assert p8(n > 50) report "a8 failed";

end architecture;
