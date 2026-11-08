-- IEEE 1850-2010 6.2.3: replicated properties.  forall i in S : p is
-- the conjunction of p for each value of i in the value set S.  All
-- must hold; see psl63 for the failing forms.

entity psl62 is
end entity;

architecture test of psl62 is
    signal clk          : bit := '0';
    signal req, ack     : bit_vector(0 to 3);
    signal req2, ack2   : bit_vector(0 to 2);
    signal sel, outv    : boolean := false;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 15 loop
            req <= "0000"; ack <= "0000"; req2 <= "000"; ack2 <= "000";
            case i is
                when 2 => req <= "1001";
                when 3 => ack <= "1001"; req <= "0100";
                when 4 => ack <= "0100";
                when 6 => req2 <= "111";
                when 7 => ack2 <= "101";        -- Instance 1 not replicated
                when others => null;
            end case;
            sel  <= (i mod 3 = 0);
            outv <= ((i - 1) mod 3 = 0) and i > 0;
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    r1: assert forall i in {0 to 3} : always (req(i) -> next ack(i));
    r2: assert forall k in boolean : always ((sel = k) -> next (outv = k));
    r3: assert forall i in {0, 2} : always (req2(i) -> next ack2(i));

end architecture;
