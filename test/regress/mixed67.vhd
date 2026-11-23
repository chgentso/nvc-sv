library ieee;
use ieee.std_logic_1164.all;

entity mixed67_core is
  generic (g_aux : integer := 0);
  port (
    clk_i : in std_logic;
    aux_i : in std_logic_vector(g_aux-1 downto 0) := (others => '0');
    q_o   : out std_logic);
end entity;

architecture a of mixed67_core is
begin
  q_o <= clk_i;
end architecture;
