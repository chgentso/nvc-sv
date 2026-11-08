-- IEEE 1850-2010 6.2.1.7: logical FL properties, failing forms.

entity psl59 is
end entity;

architecture test of psl59 is
    signal clk                 : bit := '0';
    signal a1, b1, c1, d1      : bit;
    signal e2, a2, b2          : bit;
    signal a5, b5              : bit;
    signal req, ack            : bit_vector(0 to 2);
    signal go                  : bit;
    signal okv                 : bit_vector(0 to 1);

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 19 loop
            a1 <= '0'; b1 <= '0'; c1 <= '0'; d1 <= '0';
            e2 <= '0'; a2 <= '0'; b2 <= '0';
            a5 <= '0'; b5 <= '0';
            req <= "000"; ack <= "000"; go <= '0'; okv <= "00";
            case i is
                when 2 => a1 <= '1';
                when 3 => b1 <= '1';
                when 5 => c1 <= '1';
                when 8 => a2 <= '1';
                when 11 => req <= "010";
                when 12 => ack <= "101";
                when 14 => go <= '1';
                when 17 => a5 <= '1';
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

    -- Fails at 65 ns: right conjunct, d1 not set in cycle 6
    m1: assert always ((a1 -> next b1) and (c1 -> next d1)) report "m1";
    -- Fails at 95 ns: e2 is not set and b2 not set in cycle 9
    m2: assert always (e2 or (a2 -> next b2)) report "m2";
    -- Fails at 125 ns: instance i = 1
    m3: assert always (for i in {0 to 2} : and (req(i) -> next ack(i)))
        report "m3";
    -- Fails at 155 ns: neither okv(0) nor okv(1) in cycle 15
    m4: assert always (go -> next (for i in {0 to 1} : or (okv(i))))
        report "m4";
    -- Fails at 175 ns
    m5: assert always (a5 <-> b5) report "m5";

end architecture;
