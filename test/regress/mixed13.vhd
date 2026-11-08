library ieee;
use ieee.std_logic_1164.all;

entity reg4 is
    port (clk : in std_ulogic;
          d   : in std_ulogic_vector(3 downto 0);
          q   : out std_ulogic_vector(3 downto 0));
end entity;

architecture test of reg4 is
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            q <= d;
        end if;
    end process;
end architecture;
