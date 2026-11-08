library ieee; use ieee.std_logic_1164.all;
entity svinstarr3_leaf is port (a : in std_logic_vector(3 downto 0); y : out std_logic_vector(3 downto 0)); end;
architecture a of svinstarr3_leaf is begin y <= not a; end;
