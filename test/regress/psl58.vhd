-- IEEE 1850-2010 6.2.1.7: logical FL properties.  Conjunction of two
-- temporal properties, disjunction with a Boolean left operand,
-- negation of a Boolean, implication, equivalence and parameterised
-- properties (6.2.1.7.1).  All must hold; see psl59 for the
-- failing forms.

entity psl58 is
end entity;

architecture test of psl58 is
    signal clk                 : bit := '0';
    signal a1, b1, c1, d1      : bit;
    signal e2, a2, b2          : bit;
    signal a3, b3, c3          : bit;
    signal a4, b4              : bit;
    signal req, ack            : bit_vector(0 to 2);
    signal go                  : bit;
    signal okv                 : bit_vector(0 to 1);

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 23 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0'; d1 <= '0';
            e2 <= '0'; a2 <= '0'; b2 <= '0';
            a3 <= '0'; b3 <= '0'; c3 <= '0';
            a4 <= '0'; b4 <= '0';
            req <= "000"; ack <= "000"; go <= '0'; okv <= "00";
            case i is
                when 2 => a1 <= '1';
                when 3 => b1 <= '1';
                when 5 => c1 <= '1';
                when 6 => d1 <= '1';
                when 8 => a2 <= '1'; e2 <= '1';
                when 10 => a2 <= '1';
                when 11 => b2 <= '1';
                when 13 => a3 <= '1';
                when 14 => c3 <= '1'; a4 <= '1'; b4 <= '1';
                when 15 => req <= "100";
                when 16 => ack <= "100";
                when 17 => req <= "001";
                when 18 => ack <= "001";
                when 19 => go <= '1';
                when 20 => okv <= "01";
                when 21 => go <= '1';
                when 22 => okv <= "10";
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

    l1: assert always ((a1 -> next b1) and (c1 -> next d1));
    l2: assert always (e2 or (a2 -> next b2));
    l3: assert always ((not (a3 -> b3)) -> next c3);
    l4: assert always (a4 <-> b4);
    l5: assert always (for i in {0 to 2} : and (req(i) -> next ack(i)));
    l6: assert always (go -> next (for i in {0 to 1} : or (okv(i))));

end architecture;
