-- IEEE 1850-2010 5.3: a clock expression may be a call to rose() or
-- fell().  The directive is evaluated whenever the signal in the call
-- has a rising or falling edge.

entity psl68 is
end entity;

architecture test of psl68 is
    signal clk, clk2 : bit := '0';
    signal a, b      : bit := '0';
begin

    stim: process is
    begin
        a <= '1';
        wait for 1 ns;
        clk <= '1';
        clk2 <= '1';
        wait for 1 ns;
        a <= '0';
        b <= '1';
        clk <= '0';
        clk2 <= '0';
        wait for 1 ns;
        clk <= '1';
        clk2 <= '1';
        wait for 1 ns;
        clk <= '0';
        clk2 <= '0';
        wait;
    end process;

    -- a is sampled at 1 ns, b at 3 ns
    c1: cover {a; b} @ rose(clk2) report "rose";
    -- Never covered as a is low in both falling edges
    c2: cover {a; b} @ fell(clk2) report "fell";
    -- b is high at the second rising edge (3 ns)
    c3: cover {b} @ rose(clk) report "rose2";
    -- b is high at both falling edges (2 ns and 4 ns)
    c4: cover {b} @ fell(clk) report "fell2";

end architecture;
