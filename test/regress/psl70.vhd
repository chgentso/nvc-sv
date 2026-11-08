-- IEEE 1850-2010 5.2.3.9: the bounds of a range in the value set of
-- nondet() may be any static Number expressions, not only literals.

entity psl70 is
end entity;

architecture test of psl70 is
    signal clk : bit := '0';
    constant C : integer := 10;

    default clock is rising_edge(clk);
begin

    clkgen: clk <= not clk after 1 ns when now < 40 ns;

    n1: assert always nondet({C to C + 2}) >= C;
    n2: assert always nondet({C to C + 2}) <= C + 2;
    n3: assert always nondet({2, 4 to C}) /= 3;
    n4: assert always nondet({C - 1}) = 9;

end architecture;
