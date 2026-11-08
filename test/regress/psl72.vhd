-- Replicated properties and SEREs combined with parameterised property
-- and sequence instances.  The formal parameter of a declaration may
-- bound the value set of a replicated property in its body, and the
-- parameter of a replicated property may be an actual of an instance.
-- All assertions hold, so the simulation must end without errors.
-- IEEE 1850-2010 6.2.3, 6.2.1.7.1, 6.1.1.2.5, 6.5.1, 6.5.3.

entity psl72 is
end entity;

architecture test of psl72 is
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
                when 3 => ack <= "1001"; req <= "0100";
                when 4 => ack <= "0100";
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

    a1: assert forall i in {0 to 3} : handshake(req(i), ack(i));
    a2: assert all_hs(3);
    a3: assert each_hs(3);
    a4: assert forall i in {0 to 3} : always (req_ack(i) |-> true);
    a5: assert always ({any_req(3); true} |-> (ack /= "0000"));

end architecture;
