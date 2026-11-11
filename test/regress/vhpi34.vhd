-- Depth-aware stepping of the nvc VPI extension: step over a function, a
-- recursive function and a procedure that waits, in and out of nested calls,
-- and a breakpoint inside a call that is stepped over.  The C side is
-- test/vhpi/vhpi34.c; it needs --debug-stmts.

entity vhpi34 is
end entity;

architecture test of vhpi34 is
    signal clk : bit := '0';
    signal n   : integer := 0;
    signal m   : integer := 0;
    signal k   : integer := 0;

    function inc (x : integer) return integer is
    begin
        return x + 1;
    end function;

    function twice (x : integer) return integer is
        variable r : integer;
    begin
        r := inc(x);
        return r * 2;
    end function;

    function fact (x : integer) return integer is
    begin
        if x <= 1 then
            return 1;
        end if;
        return x * fact(x - 1);
    end function;

    procedure outer (signal s : out integer; x : integer) is
        variable t : integer;
    begin
        t := twice(x);
        s <= t;
    end procedure;

    procedure slow (signal s : inout integer) is
    begin
        wait until clk = '1';
        s <= s + 1;
    end procedure;

begin
    clk <= not clk after 5 ns when now < 60 ns;

    p: process is
        variable v : integer;
    begin
        wait for 1 ns;
        v := twice(2);
        v := fact(4);
        outer(n, 5);
        slow(m);
        slow(m);
        v := v + 1;
        outer(n, 6);
        v := v + 2;
        wait;
    end process;

    q: process is
    begin
        wait for 3 ns;
        k <= k + 10;
        wait for 3 ns;
        k <= k + 10;
        wait;
    end process;
end architecture;
