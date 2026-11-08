-- Failing forms of psl72: each replicated assertion built from a
-- parameterised instance, or a parameterised declaration containing a
-- replicated property or SERE, reports the failing instance.
-- IEEE 1850-2010 6.2.3, 6.2.1.7.1, 6.5.1, 6.5.3.

entity psl73 is
end entity;

architecture test of psl73 is
    signal clk      : bit := '0';
    signal req, ack : bit_vector(0 to 3);

    default clock is rising_edge(clk);

    property handshake (boolean r, a) is always (r -> next a);
    property all_hs (const numeric n) is
        forall i in {0 to n} : always (req(i) -> next ack(i));
    property each_hs (const numeric n) is
        for i in {0 to n} : and (always (req(i) -> next ack(i)));
    sequence req_ack (const numeric k) is {req(k); ack(k)};
    sequence any_req (const numeric n) is {for i in {0 to n} : | {req(i)}};
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            req <= "0000"; ack <= "0000";
            case i is
                when 2 => req <= "1001";
                when 3 => ack <= "0001";        -- No ack(0)
                when 6 => req <= "0100";        -- Never acknowledged
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

    a1: assert forall i in {0 to 1} : handshake(req(i), ack(i))
        report "a1";
    a2: assert all_hs(1)
        report "a2";
    a3: assert each_hs(1)
        report "a3";
    a4: assert forall i in {2 to 3} : always (req_ack(i) |-> false)
        report "a4";
    a5: assert always ({any_req(3); true} |-> (ack /= "0000"))
        report "a5";

end architecture;
