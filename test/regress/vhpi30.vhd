-- Statement breakpoints of the nvc VPI extension: the callback runs before
-- a statement on a marked line, in the process that executes it, also in a
-- subprogram.  The C side is test/vpi/vhpi30.c; it needs --debug-stmts.

entity vhpi30 is
end entity;

architecture test of vhpi30 is
    signal s : integer := 0;
    signal t : integer := 0;

    procedure bump (signal x : out integer; v : integer) is
    begin
        x <= v;
    end procedure;
begin
    p_a: process is
        variable v : integer := 0;
    begin
        wait for 1 ns;
        v := v + 1;
        s <= v;
        wait for 1 ns;
        v := v + 1;
        bump(s, v);
        wait;
    end process;

    p_b: process is
    begin
        wait for 3 ns;
        t <= 100;
        wait;
    end process;
end architecture;
