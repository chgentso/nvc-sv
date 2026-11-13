entity aggtarget1 is
end entity;

architecture test of aggtarget1 is
    type rec is record
        a, b, c, d : integer;
    end record;
begin

    p1: process is
        variable r1, r2 : rec := (1, 2, 3, 4);
    begin
        -- The right hand side is evaluated before the targets are updated
        r1 := (r1.d, r1.c, r1.b, r1.a);
        assert r1 = (4, 3, 2, 1);
        (r2.d, r2.c, r2.b, r2.a) := r2;
        assert r2 = (4, 3, 2, 1);
        wait;
    end process;

end architecture;
