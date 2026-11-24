-- TEXTIO READ for REAL: decimal point required (LRM 16.4 / 15.5.2),
-- stop before malformed exponent, large integer part and overflow handling.
use std.textio.all;

entity textio_real1 is
end entity;

architecture test of textio_real1 is
begin
    process is
        variable l  : line;
        variable r  : real;
        variable ok : boolean;
    begin
        -- Integer literal without decimal point is not a valid real literal
        l := new string'("42");
        read(l, r, ok);
        assert not ok and l.all = "42";
        deallocate(l);

        l := new string'("1e3");
        read(l, r, ok);
        assert not ok and l.all = "1e3";
        deallocate(l);

        l := new string'("-16#FF#");
        read(l, r, ok);
        assert not ok and l.all = "-16#FF#";
        deallocate(l);

        -- Trailing non-exponent characters stop before 'e'
        l := new string'("1500.0ex");
        read(l, r, ok);
        assert ok and r = 1500.0 and l.all = "ex";
        deallocate(l);

        l := new string'("1500.0e+x");
        read(l, r, ok);
        assert ok and r = 1500.0 and l.all = "e+x";
        deallocate(l);

        l := new string'("1.5e3x");
        read(l, r, ok);
        assert ok and r = 1500.0 and l.all = "x";
        deallocate(l);

        -- Integer part exceeding INTEGER'HIGH is still a valid REAL
        l := new string'("999999999999999999999.0");
        read(l, r, ok);
        assert ok and r > 9.9e20 and r < 1.1e21 and l'length = 0;
        deallocate(l);

        -- Overflow beyond REAL'HIGH returns good=false without crashing
        l := new string'("1.0e500");
        read(l, r, ok);
        assert not ok and l.all = "1.0e500";
        deallocate(l);

        l := new string'("-1.0e500");
        read(l, r, ok);
        assert not ok and l.all = "-1.0e500";
        deallocate(l);

        l := new string'("16#1.0#e9999");
        read(l, r, ok);
        assert not ok and l.all = "16#1.0#e9999";
        deallocate(l);

        l := new string'("16#1.0#e-9999");
        read(l, r, ok);
        assert ok and r = 0.0 and l'length = 0;
        deallocate(l);

        wait;
    end process;
end architecture;
