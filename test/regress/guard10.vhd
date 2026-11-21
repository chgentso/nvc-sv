-- Disconnection specifications naming array elements, slices, record elements,
-- and others (LRM 7.4).

package guard10_pkg is
    function res_bit(v : bit_vector) return bit;
    subtype rbit is res_bit bit;
    type rbit_vec is array (natural range <>) of rbit;
    type rrec is record
        b : rbit;
        v : rbit_vec(0 to 1);
    end record;
end package;

package body guard10_pkg is
    function res_bit(v : bit_vector) return bit is
    begin
        for i in v'range loop
            if v(i) = '1' then
                return '1';
            end if;
        end loop;
        return '0';
    end function;
end package body;

use work.guard10_pkg.all;

entity guard10 is
    generic (td : time := 2 ns);
end entity;

architecture test of guard10 is
    signal en : boolean := true;
    signal s  : rbit bus;
    signal v  : rbit_vec(0 to 3) bus;
    signal r  : rrec bus;

    disconnect s : rbit after td;
    disconnect v(0) : rbit after td;
    disconnect v(1 to 3) : rbit_vec after td;
    disconnect r.b : rbit after td;
    disconnect r.v : rbit_vec after td;
    disconnect others : rbit after td;
begin
    blk : block (en) is
    begin
        s <= guarded '1';
        v <= guarded "1111";
        r <= guarded ('1', "11");
    end block;

    chk : process is
    begin
        wait for 1 ns;
        en <= false;
        wait for 1 ns;
        assert s = '1' and v = "1111" and r.b = '1' and r.v = "11";
        wait for 2 ns;
        assert s = '0' and v = "0000" and r.b = '0' and r.v = "00";
        wait;
    end process;
end architecture;
