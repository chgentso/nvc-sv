-- IEEE 1850-2010 6.2.1.6.2, 6.2.1.7, 6.2.3: never as an operand of a
-- replicated property and of a logical and of properties.  A failure is
-- reported in the cycle where the sequence operand of never matches.

entity psl75 is
end entity;

architecture test of psl75 is
    signal clk           : bit := '0';
    signal x             : bit_vector(0 to 1);
    signal w, y, z, q, u : bit;
    signal v             : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            x <= "00"; w <= '0'; y <= '0'; z <= '1';
            q <= '0'; u <= '0'; v <= '1';
            case i is
                when 1 | 2 => x <= "01";
                when 3 => w <= '1'; q <= '1';
                when 4 => q <= '1';
                when 5 | 6 => x <= "10";
                when 8 => y <= '1';
                when 9 => z <= '0';
                when 10 => u <= '1';
                when 11 => v <= '0';
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

    -- x(1) holds in cycles 1 and 2: fails at 25 ns; x(0) holds in
    -- cycles 5 and 6: fails at 65 ns
    b1: assert forall i in {0 to 1} : never {x(i); x(i)}
        report "b1";

    -- w holds in cycle 3: fails at 35 ns; y in cycle 8 is not followed
    -- by z: fails at 95 ns
    b2: assert (never w) and (always (y -> next z))
        report "b2";

    -- q holds in cycles 3 and 4: fails at 45 ns; u in cycle 10 is not
    -- followed by v: fails at 115 ns
    b3: assert (always (u -> next v)) and (never {q; q})
        report "b3";

    -- These hold
    g1: assert forall i in {0 to 1} : never {x(i) and x(1 - i)}
        report "g1";
    g2: assert forall i in {0 to 1} : never {x(i); x(i); x(i)}
        report "g2";
    g3: assert (never (x = "11")) and (always (x(0) -> not x(1)))
        report "g3";
    g4: assert (never {w; w}) and (never {q; q; q})
        report "g4";

end architecture;
