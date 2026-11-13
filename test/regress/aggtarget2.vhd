entity aggtarget2 is
end entity;

architecture test of aggtarget2 is
    type rec is record
        a, b, c, d : integer;
    end record;
begin

    p1: process is
        variable r1, r2 : rec := (1, 2, 3, 4);
    begin
        -- Named associations in a record aggregate target
        (d => r1.a, c => r1.b, b => r1.c, a => r1.d) := rec'(5, 6, 7, 8);
        assert r1 = (8, 7, 6, 5);
        (d => r2.a, c => r2.b, b => r2.c, a => r2.d) := r2;
        assert r2 = (4, 3, 2, 1);
        wait;
    end process;

end architecture;
