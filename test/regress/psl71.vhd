-- IEEE 1850-2010 5.2.2: b1 -> b2 is equivalent to (!b1) || b2.  As with
-- the VHDL "or" operator on BOOLEAN values, the right hand operand is
-- not evaluated if the left hand operand determines the result, so it
-- cannot raise a division by zero error.

entity psl71 is end entity;
architecture a of psl71 is
  signal clk : bit := '0';
  signal i : integer := 0;
  default clock is rising_edge(clk);
begin
  clk <= not clk after 1 ns when now < 40 ns;
  i <= i + 1 after 2 ns when i < 8;
  p1: assert always (i /= 1) -> (100 / (i - 1) /= 0);
  p2: assert always (i = 0) or (100 / i >= 0);
  p3: assert always {i /= 0; i /= 0} |-> (100 / i >= 0);
end architecture;
