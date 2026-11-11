-- Variables and parameters of the subprogram that a statement breakpoint is
-- in, through vpi_nvc_frame_variables.  The C side is test/vhpi/vhpi35.c; it
-- needs --debug-stmts.

library ieee;
use ieee.std_logic_1164.all;

entity vhpi35 is
end entity;

architecture test of vhpi35 is
    type color_t is (red, green, blue);
    type point_t is record
        x : integer;
        y : integer;
        c : color_t;
    end record;
    type int_vec is array (natural range <>) of integer;

    function scale (p : point_t; k : integer) return integer is
        variable r : integer := p.x * k;
        variable q : integer;
    begin
        q := r + p.y;
        return q;
    end function;

    function mix (v : std_logic_vector; w : bit_vector(3 downto 0);
                  s : string; r : real; t : time) return integer is
        variable acc : integer := 0;
        variable lv  : std_logic_vector(v'range) := v;
        variable col : color_t := green;
        variable ar  : int_vec(1 to 3) := (10, 20, 30);
        variable sv  : string(1 to 3) := "abc";
        variable p   : point_t := (1, 2, blue);
        variable ok  : boolean := true;
    begin
        acc := v'length + w'length + s'length;
        acc := acc + scale(p, 3);
        return acc;
    end function;

    function fact (n : natural) return natural is
        variable here : natural := n;
    begin
        if n <= 1 then
            return 1;
        end if;
        return n * fact(n - 1);
    end function;

    procedure bump (variable cnt : inout integer; constant step : in integer;
                    signal s : out integer) is
        variable tmp : integer;
    begin
        tmp := cnt + step;
        cnt := tmp;
        s <= tmp;
    end procedure;

    procedure waits (x : in integer; signal clk : in bit) is
        variable local : integer := x * 2;
    begin
        wait until clk = '1';
        local := local + 1;
        assert local > 0;
    end procedure;

    signal clk : bit := '0';
    signal sig : integer := 0;
begin
    clk <= not clk after 5 ns when now < 40 ns;

    p: process is
        variable n  : integer := 0;
        variable res : integer;
        constant pt : point_t := (4, 5, red);
    begin
        res := scale(pt, 2);
        res := mix("10Z1", "0110", "hi!", 2.5, 3 ns);
        res := fact(3);
        bump(n, 5, sig);
        waits(7, clk);
        wait;
    end process;
end architecture;
