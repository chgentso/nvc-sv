use std.textio.all;

entity textio_based is
end entity;

architecture test of textio_based is
begin
    process is
        variable l : line;
        variable i : integer;
        variable r : real;
        variable bv : bit_vector(1 to 4);
        variable ok : boolean;
    begin
        l := new string'("2#1010#;");
        read(l, i);
        assert i = 10 and l.all = ";";

        l := new string'("16#FF#x");
        read(l, i);
        assert i = 255 and l.all = "x";

        l := new string'("-16#1_0#");
        read(l, i, ok);
        assert ok and i = -16;

        l := new string'("1e3x");
        read(l, i);
        assert i = 1000 and l.all = "x";

        l := new string'("5e");
        read(l, i);
        assert i = 5 and l.all = "e";

        l := new string'("16#F.8#;");
        read(l, r);
        assert r = 15.5 and l.all = ";";

        l := new string'("2#1.1#E1 ");
        read(l, r);
        assert r = 3.0;

        l := new string'("16#FF");   -- Unterminated
        read(l, i, ok);
        assert not ok;

        l := new string'("+");
        read(l, i, ok);
        assert not ok;

        l := new string'("1_000");
        read(l, i);
        assert i = 1000;

        l := new string'("1__001");
        read(l, bv, ok);
        assert not ok;

        l := new string'("1_0_0_1");
        read(l, bv, ok);
        assert ok and bv = "1001" and l'length = 0;

        l := new string'("1001_x");
        read(l, bv);
        assert bv = "1001" and l.all = "_x";
        wait;
    end process;
end architecture;
