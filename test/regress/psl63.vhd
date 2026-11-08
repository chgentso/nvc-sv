-- IEEE 1850-2010 6.2.3: replicated properties, failing forms.  Each
-- failing instance is reported when it fails.

entity psl63 is
end entity;

architecture test of psl63 is
    signal clk          : bit := '0';
    signal req, ack     : bit_vector(0 to 3);
    signal sel, outv    : boolean := false;

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 11 loop
            req <= "0000"; ack <= "1111";
            sel <= false; outv <= false;
            case i is
                when 3 => req <= "0010";
                when 4 => ack <= "1101";
                when 6 => req <= "1000";
                when 7 => ack <= "0111";
                when 9 => sel <= true;
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

    -- Fails at 45 ns (i = 2) and 75 ns (i = 0)
    b1: assert forall i in {0 to 3} : always (req(i) -> next ack(i))
        report "b1";
    -- Fails at 105 ns (k = true)
    b2: assert forall k in boolean : always ((sel = k) -> next (outv = k))
        report "b2";

end architecture;
