library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package mixed17_pkg is
    constant K : std_logic_vector(3 downto 0) := "1010";
end package;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.mixed17_pkg.all;

entity mixed_def is
    generic (A : std_logic_vector(7 downto 0) := (others => '0');
             B : std_logic_vector(7 downto 0) := (7 downto 4 => '1',
                                                  others => '0');
             C : unsigned(3 downto 0) := to_unsigned(5, 4);
             D : std_logic_vector(3 downto 0) := K;
             E : std_logic_vector(2 downto 0) := ('1', '0', 'Z');
             F : bit_vector(1 to 3) := (others => '1'));
    port (o : out std_logic);
end entity;

architecture test of mixed_def is
begin
    process is
    begin
        if A = x"00" then
            assert B = x"F0" and C = "0101" and D = "1010" and E = "10Z"
                and F = "111"
                report "FAILED: defaults";
        elsif A = x"12" then
            assert B = x"F0" and C = "0101" and D = "1010" and E = "10Z"
                and F = "010"
                report "FAILED: override";
        else
            report "FAILED: A=" & to_string(A) severity failure;
        end if;
        o <= '1';
        wait;
    end process;
end architecture;
