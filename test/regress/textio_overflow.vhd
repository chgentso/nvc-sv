use std.textio.all;

entity textio_overflow is
end entity;

architecture test of textio_overflow is
    procedure check_bad (str : string) is
        variable l  : line;
        variable i  : integer;
        variable ok : boolean;
    begin
        l := new string'(str);
        i := 42;
        read(l, i, ok);
        assert not ok report "read of " & str & " should fail";
        assert l.all = str report "line was consumed for " & str;
        deallocate(l);
    end procedure;

    procedure check_good (str : string; expect : integer; rest : string := "") is
        variable l  : line;
        variable i  : integer;
        variable ok : boolean;
    begin
        l := new string'(str);
        read(l, i, ok);
        assert ok report "read of " & str & " failed";
        assert i = expect report "bad value for " & str & ": " & integer'image(i);
        assert l.all = rest report "bad remainder for " & str;
        deallocate(l);
    end procedure;

    procedure check_time_bad (str : string) is
        variable l  : line;
        variable t  : time;
        variable ok : boolean;
    begin
        l := new string'(str);
        read(l, t, ok);
        assert not ok report "read of " & str & " should fail";
        deallocate(l);
    end procedure;
begin
    process is
        variable l : line;
        variable t : time;
        variable ok : boolean;
    begin
        check_bad("2147483648");
        check_bad("-2147483649");
        check_bad("99999999999999999999");
        check_bad("-99999999999999999999");
        check_bad("1e10");
        check_bad("16#100000000#");
        check_bad("16#FFFFFFFF#e1");
        check_bad("2#1#e31");
        check_bad("1e99999999999");
        check_bad("99999999999#1#");
        check_bad("-2#1#e32");
        check_bad("1_000_000_000_0");

        check_good("2147483647", integer'high);
        check_good("+2147483647", integer'high);
        check_good("-2147483648", integer'low);
        check_good("2#1#e30", 2 ** 30);
        check_good("-2#1#e31", integer'low);
        check_good("16#7FFFFFFF#", integer'high);
        check_good("-16#80000000#", integer'low);
        check_good("0000000000000000000000012", 12);
        check_good("0e99999999999", 0);
        check_good("2_147_483_647 x", integer'high, " x");
        check_good("1e9", 1000000000);
        check_good("214748364e1", 2147483640);

        check_time_bad("9999999999 hr");
        check_time_bad("3 hr");
        check_time_bad("-99999999999999 sec");

        l := new string'("2 hr");
        read(l, t, ok);
        assert ok and t = 2 hr;

        wait;
    end process;
end architecture;
