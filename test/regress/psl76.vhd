-- IEEE 1850-2010 6.1.1.1, 6.2.1.3.2, 6.2.1.6.1, 6.2.1.6.2: a repeated
-- SERE is a Sequence and can be the left hand operand of a suffix
-- implication and the operand of never and eventually!.

entity psl76 is
end entity;

architecture test of psl76 is
    signal clk     : bit := '0';
    signal a, b, d : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            a <= '0'; b <= '0'; d <= '0';
            case i is
                when 2 | 6 => a <= '1';
                when 3 | 7 => a <= '1'; b <= '1';
                when 4 => d <= '1';
                when 8 => a <= '1'; d <= '1';
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

    -- {a}[*2] ends in cycles 3, 7 and 8 but b does not hold in cycle 8
    a1: assert always {a}[*2] |-> b report "a1";

    -- d does not hold in cycle 9
    a2: assert always {a}[*2] |=> d report "a2";

    -- b holds in cycles 3 and 7
    a3: assert never {b}[*1 to 2] report "a3";

    -- These hold
    g1: assert always {a}[*1 to 2] |-> a report "g1";
    g2: assert always {a; b}[+] |=> not b report "g2";
    g3: assert never {b}[*2] report "g3";
    g4: assert eventually! {a}[*3] report "g4";

end architecture;
