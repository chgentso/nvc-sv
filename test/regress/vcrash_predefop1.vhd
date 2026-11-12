-- Predefined operators work on values of the base type so the operands
-- need not belong to the subtype of the formal
entity vcrash_predefop1 is
end entity;

architecture test of vcrash_predefop1 is
    type small is range 0 to 3;
    type prob is range 0.0 to 1.0;
begin
    process is
        variable s : small := 2;
        variable h : prob := 1.0;
    begin
        s := s / 4 + 1;
        assert s = 1;
        h := h * 0.5;
        h := h / 2.0;
        assert h = 0.25;
        wait;
    end process;
end architecture;
