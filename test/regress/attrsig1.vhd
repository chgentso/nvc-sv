entity attrsig1 is
end entity;

architecture test of attrsig1 is
    type state_t is (idle, busy);
    type other_t is (idle, done);

    attribute encoding : string;
    attribute encoding of idle [return state_t] : literal is "00";
    attribute encoding of busy : literal is "01";

    procedure check (s : string) is
    begin
        assert s = "00" report "bad: " & s severity failure;
    end procedure;
begin
    p: process is
    begin
        -- A signature in the prefix of an attribute name used as an
        -- actual parameter must not be taken for a formal designator
        check(idle [return state_t]'encoding);
        assert idle [return state_t]'encoding = "00";
        wait;
    end process;
end architecture;
