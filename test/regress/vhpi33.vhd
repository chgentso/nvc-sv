-- The call stack and stepping of the nvc VPI extension: the frames of a
-- statement callback inside nested subprogram calls, whether a line has a
-- statement, and stepping in one process that does not stop in the library
-- functions the simulator calls for a wait.  The C side is test/vhpi/vhpi33.c;
-- it needs --debug-stmts.

library ieee;
use ieee.std_logic_1164.all;

entity vhpi33 is
end entity;

architecture test of vhpi33 is
    signal clk : std_logic := '0';
    signal n   : integer := 0;
    signal m   : integer := 0;
    signal k   : integer := 0;
    function inner (x : integer) return integer is
        variable loc : integer := x * 2;
    begin
        loc := loc + 1;
        return loc;
    end function;

    procedure outer (signal s : out integer; x : integer) is
        variable t : integer;
    begin
        t := inner(x);
        s <= t;
    end procedure;

    -- A comment line and
    -- a blank line follow

begin
    clk <= not clk after 5 ns when now < 40 ns;

    p: process is
    begin
        wait until rising_edge(clk);
        outer(n, 5);
        wait until rising_edge(clk);
        wait until rising_edge(clk);
        wait;
    end process;

    q: process is
    begin
        wait for 3 ns;
        m <= m + 1;
        wait for 3 ns;
        m <= m + 1;
        wait for 3 ns;
        m <= m + 1;
        wait;
    end process;

    -- A sensitivity list: the condition is evaluated by the scheduler
    r: process (clk) is
    begin
        if rising_edge(clk) then
            k <= k + 1;
        end if;
    end process;

    -- Reports: the place is the statement, not the start of the subprogram
    -- or the process
    s: process is
        function f (x : integer) return integer is
        begin
            assert x /= 1 report "in f" severity note;
            return x;
        end function;
        variable v : integer;
    begin
        wait for 1 ns;
        report "in s";
        v := f(1);
        wait;
    end process;
end architecture;
