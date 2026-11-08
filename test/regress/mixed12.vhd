library ieee; use ieee.std_logic_1164.all;
entity inout_if is
  port (oe : in std_logic; d_in : in std_logic_vector(7 downto 0);
        d : inout std_logic_vector(7 downto 0);
        q : out std_logic_vector(7 downto 0);
        s : inout std_logic);
end;
architecture a of inout_if is
begin
  d <= d_in when oe = '1' else (others => 'Z');
  q <= d;
  s <= '1' when oe = '1' else 'Z';
end;
