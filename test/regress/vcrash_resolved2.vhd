-- Wired-or resolution of an array of vectors with the element type
-- given by a constrained subtype
library ieee;
use ieee.std_logic_1164.all;

entity vcrash_resolved2 is
end entity;

architecture test of vcrash_resolved2 is
    subtype nibble is integer range 3 downto 0;
    type nibble_array is array (natural range <>) of std_logic_vector(nibble);

    function resolve (s : nibble_array) return std_logic_vector is
        variable r : std_logic_vector(nibble) := (others => '0');
    begin
        for i in s'range loop
            r := r or s(i);
        end loop;
        return r;
    end function;

    subtype rnibble is resolve std_logic_vector(nibble);
    signal sig : rnibble;
begin
    sig <= "0101";
    sig <= "0011";

    process is
    begin
        wait for 1 ns;
        assert sig = "0111" report to_string(sig);
        wait;
    end process;
end architecture;
