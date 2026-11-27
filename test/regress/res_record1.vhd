package res_record1_pkg is
    function sum_int (v : integer_vector) return integer;
    function wired_or (v : bit_vector) return bit;

    type inner_t is record
        x : integer;
    end record;

    type pkt_t is record
        n   : integer;
        f   : bit;
        vec : bit_vector(1 downto 0);
        sub : inner_t;
    end record;

    subtype res_pkt_t is (
        n   sum_int,
        f   wired_or,
        vec (wired_or),
        sub (x sum_int)
    ) pkt_t;

    type pkt_arr_t is array (0 to 1) of pkt_t;
    subtype res_pkt_arr_t is ((n sum_int, f wired_or, vec (wired_or), sub (x sum_int))) pkt_arr_t;
end package;

package body res_record1_pkg is
    function sum_int (v : integer_vector) return integer is
        variable acc : integer := 0;
    begin
        for i in v'range loop
            acc := acc + v(i);
        end loop;
        return acc;
    end function;

    function wired_or (v : bit_vector) return bit is
        variable acc : bit := '0';
    begin
        for i in v'range loop
            acc := acc or v(i);
        end loop;
        return acc;
    end function;
end package body;

use work.res_record1_pkg.all;

entity res_record1 is
end entity;

architecture beh of res_record1 is
    constant ZERO_PKT : pkt_t := (n => 0, f => '0', vec => "00", sub => (x => 0));
    signal s1 : res_pkt_t := ZERO_PKT;
    signal s2 : (n sum_int, f wired_or, vec (wired_or)) pkt_t := ZERO_PKT;
    signal sa : res_pkt_arr_t := (others => ZERO_PKT);
begin
    s1 <= (n => 3, f => '0', vec => "10", sub => (x => 10));
    s1 <= (n => 4, f => '1', vec => "01", sub => (x => 25));

    s2.n   <= 1;
    s2.n   <= 2;
    s2.f   <= '0';
    s2.f   <= '1';
    s2.vec <= "10";
    s2.vec <= "00";

    sa(0) <= (n => 5, f => '1', vec => "01", sub => (x => 2));
    sa(0) <= (n => 6, f => '0', vec => "10", sub => (x => 3));

    check: process is
    begin
        wait for 1 ns;
        assert s1.n = 7 report "s1.n=" & integer'image(s1.n) severity failure;
        assert s1.f = '1' severity failure;
        assert s1.vec = "11" severity failure;
        assert s1.sub.x = 35 report "s1.sub.x=" & integer'image(s1.sub.x) severity failure;

        assert s2.n = 3 report "s2.n=" & integer'image(s2.n) severity failure;
        assert s2.f = '1' severity failure;
        assert s2.vec = "10" severity failure;

        assert sa(0).n = 11 report "sa(0).n=" & integer'image(sa(0).n) severity failure;
        assert sa(0).f = '1' severity failure;
        assert sa(0).vec = "11" severity failure;
        assert sa(0).sub.x = 5 severity failure;
        wait;
    end process;
end architecture;
