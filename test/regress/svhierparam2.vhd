library ieee;
use ieee.std_logic_1164.all;
entity svhierparam2_ent is
  generic (g_w : integer := 1; g_n : integer := 1);
  port (a : in std_logic_vector(g_w-1 downto 0); q : out std_logic_vector(g_n-1 downto 0));
end;
architecture a of svhierparam2_ent is
begin
  q <= (others => a(0));
  process begin
    wait for 1 ns;
    report "ent g_w=" & integer'image(g_w) & " g_n=" & integer'image(g_n);
    wait;
  end process;
end;
