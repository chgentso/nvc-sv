-- Reads of std_logic_vector signals of several widths holding each of the
-- nine values, repeatedly, so that a read cannot return a stale value.
-- The C side is test/vhpi/vhpi28.c.

library ieee;
use ieee.std_logic_1164.all;

entity vhpi28 is
end entity;

architecture test of vhpi28 is
    signal s1 : std_logic_vector(0 downto 0);
    signal s7 : std_logic_vector(6 downto 0);
    signal s8 : std_logic_vector(7 downto 0);
    signal s9 : std_logic_vector(8 downto 0);
    signal s64 : std_logic_vector(63 downto 0);
    signal s65 : std_logic_vector(64 downto 0);
    signal s130 : std_logic_vector(129 downto 0);
    signal arr : std_logic_vector(0 to 8);
    constant lit : string(1 to 9) := "UX01ZWLH-";

    function pattern (w, ph : natural) return std_logic_vector is
        variable r : std_logic_vector(w - 1 downto 0);
    begin
        for i in r'range loop
            r(i) := std_logic'value("'" & lit((i + ph) mod 9 + 1) & "'");
        end loop;
        return r;
    end function;
begin
    process is
    begin
        for ph in 0 to 8 loop
            s1 <= pattern(1, ph);
            s7 <= pattern(7, ph);
            s8 <= pattern(8, ph);
            s9 <= pattern(9, ph);
            s64 <= pattern(64, ph);
            s65 <= pattern(65, ph);
            s130 <= pattern(130, ph);
            arr <= pattern(9, ph);
            wait for 10 ns;
        end loop;
        wait;
    end process;
end architecture;
