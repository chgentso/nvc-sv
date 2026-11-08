-- IEEE 1850-2010 5.3 and 5.4: clock expressions and default clock.
-- A clock expression may be a Boolean built-in function call, a VHDL
-- function call or a parenthesised Boolean.  An explicit clock takes
-- precedence over the default clock.  A default clock declaration in a
-- block applies to directives in that block; a block without one uses
-- the default clock of the enclosing architecture.
--
-- In cycle i, clk rises at 10i+5 ns and falls at 10i+8 ns, clk2 rises
-- with clk in even cycles.  Signals ending in r change at 10i ns and
-- signals ending in f change at 10i+6 ns, so a directive sampled on the
-- wrong edge sees different values.

entity psl31 is
end entity;

architecture test of psl31 is
    signal clk, clk2 : bit := '0';
    signal a1r, b1r, a2f, b2f, a3r, b3r, a4r, b4r : bit;
    signal a5f, b5f, a6r, b6r, a7r, b7r           : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 23 loop
            a1r <= '0'; b1r <= '0'; a3r <= '0'; b3r <= '0';
            a4r <= '0'; b4r <= '0'; a6r <= '0'; b6r <= '0';
            a7r <= '0'; b7r <= '0';
            case i is
                when 1 => a1r <= '1';
                when 2 => b1r <= '1';
                when 10 => a6r <= '1';
                when 11 => b6r <= '1';
                when 12 => a3r <= '1';
                when 14 => b3r <= '1';
                when 16 => a4r <= '1';
                when 18 => b4r <= '1';
                when 20 => a7r <= '1';
                when 21 => b7r <= '0';
                when 22 => b7r <= '1';
                when others => null;
            end case;
            wait for 5 ns;
            clk <= '1';
            if i mod 2 = 0 then
                clk2 <= '1';
            end if;
            wait for 1 ns;
            a2f <= '0'; b2f <= '0'; a5f <= '0'; b5f <= '0';
            case i is
                when 5 => a2f <= '1';
                when 6 => b2f <= '1';
                when 8 => a5f <= '1';
                when 9 => b5f <= '1';
                when others => null;
            end case;
            wait for 2 ns;
            clk  <= '0';
            clk2 <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    k1: cover {a1r; b1r} report "k1";                     -- 25 ns
    k2: cover {a2f; b2f} @ falling_edge(clk) report "k2";  -- 68 ns
    k3: cover {a3r; b3r} @ rose(clk2) report "k3";         -- 145 ns
    k4: cover {a4r; b4r} @ (clk2'event and clk2 = '1')
        report "k4";                                       -- 185 ns

    b1: block is
        default clock is falling_edge(clk);
    begin
        k5: cover {a5f; b5f} report "k5";                 -- 98 ns
    end block;

    b2: block is
    begin
        k6: cover {a6r; b6r} report "k6";                 -- 115 ns
    end block;

    -- Must hold: only true when sampled on clk2
    h1: assert (always (a7r -> next b7r)) @ rising_edge(clk2);

end architecture;
