-- IEEE 1850-2010 6.1.1.2.5, 6.1.2.5, 6.2.1.6.1: a braced parameterised
-- SERE and a clocked SERE are Sequences and can be the left hand operand
-- of a suffix implication or the operand of never.

entity psl77 is
end entity;

architecture test of psl77 is
    signal clk  : bit := '0';
    signal x    : bit_vector(0 to 1);
    signal y, z : bit;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            x <= "00"; y <= '0'; z <= '0';
            case i is
                when 1 => x <= "11"; y <= '1';
                when 3 | 4 => x <= "10";
                when 5 => z <= '1';
                when 6 => x <= "11";
                when 8 | 9 => x <= "01";
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

    -- x is "11" in cycles 1 and 6 but y does not hold in cycle 6
    a1: assert always {for i in {0 to 1} : && {x(i)}} |-> y report "a1";

    -- x(0) holds in cycles 3 and 4 and z in cycle 5; x(1) holds in
    -- cycles 8 and 9 but z does not hold in cycle 10
    a2: assert always {for i in {0 to 1} : | {x(i); x(i)}} |=> z
        report "a2";

    -- These hold
    g1: assert always {for i in {0 to 1} : && {x(i)}} |-> x(0)
        report "g1";
    g2: assert never {for i in {0 to 1} : && {x(i); x(i)}} report "g2";
    g3: assert always {x(0); x(0)}@rising_edge(clk) |=> z report "g3";

end architecture;
