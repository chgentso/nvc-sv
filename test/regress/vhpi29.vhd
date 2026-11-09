-- Force, deposit and release of signals through vhpi_put_value and the
-- vhpiIsForcedP property.  The C side is test/vhpi/vhpi29.c.

library ieee;
use ieee.std_logic_1164.all;

entity vhpi29 is
end entity;

architecture test of vhpi29 is
    type rec_t is record
        a : bit;
        b : std_logic_vector(1 downto 0);
    end record;

    signal s : std_logic_vector(3 downto 0) := "0000";
    signal r : rec_t := ('0', "00");
    signal x : integer := 0;
begin
    -- Drive the signals every 10 ns so that a force can be told apart
    process is
    begin
        for i in 1 to 4 loop
            wait for 10 ns;
            s <= "0101";
            r.a <= '1';
            r.b <= "11";
            x <= i;
        end loop;
        wait;
    end process;
end architecture;
