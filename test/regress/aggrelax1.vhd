library ieee;
use ieee.std_logic_1164.all;

entity aggrelax1 is
end entity;

architecture test of aggrelax1 is
    function get_n return natural is
    begin
        return 6;
    end function;

    constant n : natural := get_n;
    signal adr : std_logic_vector(31 downto 0);
begin

    adr <= (n - 1 downto 0 => '0', others => '1');

    process is
    begin
        wait for 1 ns;
        assert adr = x"FFFFFFC0";
        wait;
    end process;

end architecture;
