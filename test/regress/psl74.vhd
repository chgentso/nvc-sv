-- The PSL keywords forall, next_event_a, next_event_a!, next_event_e and
-- next_event_e! are reserved inside PSL declarations and directives
-- written directly in VHDL-2008, and remain identifiers elsewhere.
-- All assertions hold, so the simulation must end without errors.
-- IEEE 1076-2008 15.10; IEEE 1850-2010 6.2.1.6, 6.2.3.

entity psl74_ent is
    port (clk : in bit; req, ack : in bit_vector(0 to 3));
    default clock is rising_edge(clk);
    property ent_hs is forall i in {0 to 3} : always (req(i) -> next ack(i));
begin
    e1: assert ent_hs;
    e2: assert always (req(3) -> next_event_a(ack(3))[1 to 1](ack(0)));
end entity;

architecture test of psl74_ent is
begin
end architecture;

entity psl74 is
end entity;

architecture test of psl74 is
    signal clk      : bit := '0';
    signal req, ack : bit_vector(0 to 3);
    signal forall   : boolean := true;      -- Not reserved outside PSL

    default clock is rising_edge(clk);

    property hs is forall i in {0 to 3} : always (req(i) -> next ack(i));
    property ev_a is always (req(0) -> next_event_a(ack(0))[1 to 1](req(1)));
    property ev_a1 is always (req(0) -> next_event_a!(ack(2))[1 to 1](true));
    property ev_e is always (req(0) -> next_event_e(ack(0))[1 to 1](req(2)));
    property ev_e1 is always (req(0) -> next_event_e!(ack(2))[1 to 1](true));
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            req <= "0000"; ack <= "0000";
            case i is
                when 2 => req <= "1001";
                when 3 => ack <= "1001"; req <= "0110";
                when 4 => ack <= "0110";
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

    a1: assert hs;
    a2: assert forall i in {0 to 3} : always (req(i) -> next ack(i));
    a3: assert ev_a;
    a4: assert ev_a1;
    a5: assert ev_e;
    a6: assert ev_e1;
    a7: assume forall i in {1 to 2} : always (req(i) -> next ack(i));
    forall <= not forall when rising_edge(clk);

    u: entity work.psl74_ent port map (clk, req, ack);

end architecture;
