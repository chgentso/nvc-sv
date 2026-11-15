package guarded_composite_pkg is
    function res (v : bit_vector) return bit;
    subtype rbit is res bit;
    type rbit_vector is array (natural range <>) of rbit;
    type rrec is record
        b : rbit;
        v : rbit_vector(0 to 1);
    end record;
end package;

package body guarded_composite_pkg is
    function res (v : bit_vector) return bit is
    begin
        for k in v'range loop
            if v(k) = '1' then return '1'; end if;
        end loop;
        return '0';
    end function;
end package body;

-------------------------------------------------------------------------------

use work.guarded_composite_pkg.all;

entity guarded_composite is
end entity;

architecture test of guarded_composite is
    signal en : boolean := true;
    signal v : rbit_vector(0 to 3) bus;     -- Every element is resolved
    signal rr : rrec bus;
begin
    b: block (en) is
    begin
        v <= guarded "1011";
        rr <= guarded ('1', "01");
    end block;

    process is
    begin
        wait for 1 ns;
        assert v = "1011";
        assert rr.b = '1' and rr.v = "01";
        wait;
    end process;
end architecture;
