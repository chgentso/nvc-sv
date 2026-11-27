-- Hand-written case: a failing assertion ends the run with a non-zero status.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tb is
end entity;

architecture sim of tb is
    signal clk : std_logic := '0';
    signal n   : unsigned(3 downto 0) := (others => '0');
begin
    clk <= not clk after 5 ns;

    p_n : process (clk) is
    begin
        if rising_edge(clk) then
            n <= n + 1;
        end if;
    end process;

    p_chk : process (n) is
    begin
        assert n /= 5 report "n reached 5" severity failure;
    end process;
end architecture;
