library ieee;
use ieee.std_logic_1164.all;

entity mixed44_sub is
    port (clk : in std_logic;
          io  : inout std_logic_vector(1 downto 0);
          seen : out std_logic_vector(1 downto 0));
end entity;

architecture test of mixed44_sub is
    signal tick : std_logic := '0';
begin
    -- The driver of the port is evaluated again each cycle but always
    -- releases it
    io <= (others => 'Z') when tick = '0' else (others => 'Z');

    process (clk) is
    begin
        if rising_edge(clk) then
            tick <= not tick;
            seen <= io;
        end if;
    end process;
end architecture;
