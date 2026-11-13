entity relaxed1 is
end entity;

architecture test of relaxed1 is
    type t1 is range -32768 to 0;
    type phys is range t1'(-32000) to 32000 units
        a;
        b = 10 a;
    end units;
begin

    p1: process is
        variable x : phys := 5 a;
    begin
        assert phys'pos(phys'low) = -32000;
        assert phys'pos(phys'high) = 32000;
        assert x = 5 a;
        wait;
    end process;

end architecture;
