-- IEEE 1850-2010 6.2.1.5.2: failing before! properties.  The strong form
-- fails at the end of the simulation if the left operand never holds.

entity psl57 is
end entity;

architecture test of psl57 is
    signal clk                 : bit := '0';
    signal r0, a0, b0          : bit;
    signal r1, a1, b1          : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            r0 <= '0'; a0 <= '0'; b0 <= '0';
            r1 <= '0'; a1 <= '0'; b1 <= '0';
            case i is
                when 2 => r0 <= '1';
                when 4 => b0 <= '1';
                when 5 => a0 <= '1';
                when 8 => r1 <= '1';
                when others => null;
            end case;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Fails at 45 ns: b0 holds before a0
    w0: assert always (r0 -> next (a0 before! b0)) report "w0";
    -- Fails at the end: neither a1 nor b1 ever holds
    w1: assert always (r1 -> next (a1 before! b1)) report "w1";

end architecture;
