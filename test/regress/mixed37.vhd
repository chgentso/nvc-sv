library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed37 is
end entity;

architecture test of mixed37 is
    constant c_len : natural := 100;

    function ceil_log2 (x : natural) return natural is
        variable n : natural := 0;
    begin
        while 2**n < x loop
            n := n + 1;
        end loop;
        return n;
    end function;

    component mixed37_sub is
        generic (a, b, c : integer);
        port (o : out std_logic_vector(7 downto 0));
    end component;

    signal o : std_logic_vector(7 downto 0);
begin
    u: component mixed37_sub
        generic map (a => ceil_log2(c_len), b => c_len / 50, c => ceil_log2(2))
        port map (o);

    check: process is
    begin
        wait for 1 ns;
        assert unsigned(o) = 7 * 16 + 2 + 1;
        wait;
    end process;
end architecture;
