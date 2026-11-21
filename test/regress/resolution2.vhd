-- Resolution function on an unconstrained array subtype instantiated with a
-- constrained signal subtype and returning a different index range (LRM 14.7.3.4).

package resolution2_pkg is
    type bv_arr is array (natural range <>) of bit_vector;
    function or_all(vals : bv_arr) return bit_vector;
    subtype res_bv is or_all bit_vector;
end package;

package body resolution2_pkg is
    function or_all(vals : bv_arr) return bit_vector is
        variable acc : bit_vector(7 downto 4) := "0000";
    begin
        for i in vals'range loop
            acc := acc or vals(i);
        end loop;
        return acc;
    end function;
end package body;

use work.resolution2_pkg.all;

entity resolution2 is
end entity;

architecture test of resolution2 is
    signal v : res_bv(0 to 3);
    subtype res_bv4 is res_bv(0 to 3);
    signal w : res_bv4;
begin
    p1 : process is
    begin
        wait for 1 ns;
        v <= "1000";
        w <= "0010";
        wait;
    end process;

    p2 : process is
    begin
        wait for 1 ns;
        v <= "0100";
        w <= "0001";
        wait;
    end process;

    chk : process is
    begin
        wait for 2 ns;
        assert v = "1100" and v'left = 0 and v(0) = '1' and v(3) = '0';
        assert w = "0011" and w'left = 0 and w(2) = '1' and w(3) = '1';
        wait;
    end process;
end architecture;
