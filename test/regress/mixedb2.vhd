library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity mixedb2_e is
  generic (g_a : integer := 1);
  port (xo : out std_logic_vector(7 downto 0);
        yo : out std_logic_vector(7 downto 0));
end entity;
architecture a of mixedb2_e is begin
  xo <= std_logic_vector(to_unsigned(g_a, 8));
  yo <= x"34";
end architecture;
