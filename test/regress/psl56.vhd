-- IEEE 1850-2010 6.2.1.5.3: failing until! properties.  The strong form
-- fails at the end of the simulation if the right operand never holds.

entity psl56 is
end entity;

architecture test of psl56 is
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
                when 5 => r0 <= '1';
                when 6 => a0 <= '1';
                when 8 => r1 <= '1';
                when others => null;
            end case;
            if i > 8 then
                a1 <= '1';
            end if;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- Fails at 75 ns: a0 drops before b0 holds
    z0: assert always (r0 -> next (a0 until! b0)) report "z0";
    -- Fails at the end: b1 never holds
    z1: assert always (r1 -> next (a1 until! b1)) report "z1";

end architecture;
