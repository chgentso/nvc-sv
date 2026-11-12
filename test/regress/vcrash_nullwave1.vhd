-- A null transaction after another waveform element must not discard it
package vcrash_nullwave1_pkg is
    function res (v : bit_vector) return bit;
    subtype rbit is res bit;
end package;

package body vcrash_nullwave1_pkg is
    function res (v : bit_vector) return bit is
    begin
        for k in v'range loop
            if v(k) = '1' then
                return '1';
            end if;
        end loop;
        return '0';
    end function;
end package body;

-------------------------------------------------------------------------------

use work.vcrash_nullwave1_pkg.all;

entity vcrash_nullwave1 is
end entity;

architecture test of vcrash_nullwave1 is
    signal g, h : rbit bus;
begin
    process is
    begin
        g <= '1', null after 2 ns;
        h <= '1';
        wait for 1 ns;
        assert g = '1';
        assert h = '1';
        wait for 2 ns;
        assert g = '0';
        wait;
    end process;
end architecture;
