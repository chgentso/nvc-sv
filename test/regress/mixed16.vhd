library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed_vec is
    generic (MASK : std_logic_vector(7 downto 0) := x"A5";
             INIT : unsigned(3 downto 0) := "0011";
             BV   : bit_vector := "110";
             PAT  : std_logic_vector(3 downto 0) := "0000");
    port (o : out std_logic_vector(7 downto 0));
end entity;

architecture test of mixed_vec is
begin
    process is
    begin
        if MASK = x"A5" then
            assert INIT = "0011" and BV = "110" and PAT = "0000"
                report "FAILED: defaults";
        elsif MASK = x"3C" then
            assert INIT = "1001" and BV = "10101" and PAT = "ZX01"
                report "FAILED: named";
        elsif MASK = x"FF" then
            assert INIT = "0001" and BV = "01" and PAT = "0000"
                report "FAILED: positional";
        else
            report "FAILED: MASK=" & to_string(MASK) severity failure;
        end if;
        o <= MASK;
        wait;
    end process;
end architecture;
