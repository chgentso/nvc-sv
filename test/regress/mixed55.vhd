library ieee;
use ieee.std_logic_1164.all;

entity mixed55_sub is
    generic (
        g_rst  : std_logic := '0';
        g_def  : std_ulogic := '1';
        g_z    : std_logic := '0');
    port (
        x : out std_logic_vector(2 downto 0));
end entity;

architecture a of mixed55_sub is
begin
    x <= (0 => g_rst, 1 => g_def, 2 => g_z);
end architecture;
