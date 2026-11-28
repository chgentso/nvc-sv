-- Hand-written case: a failing assertion ends the run with a non-zero status.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m0 is
    port (clk : in std_logic;
          o0  : out unsigned(3 downto 0));
end entity;

architecture rtl of m0 is
    signal n : unsigned(3 downto 0) := (others => '0');
begin
    p0 : process (clk) is
    begin
        if rising_edge(clk) then
            n <= n + 1;
        end if;
    end process;

    o0 <= n;

    p1 : process (n) is
    begin
        assert n /= 5 report "n reached 5" severity failure;
    end process;
end architecture;
