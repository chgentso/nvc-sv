library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity genadd is
  generic (W : positive := 4; INV : boolean := false; K : natural);
  port (a, b : in std_logic_vector(W-1 downto 0);
        y : out std_logic_vector(W-1 downto 0));
end;
architecture a of genadd is
begin
  process (a, b) begin
    if INV then y <= std_logic_vector(unsigned(a) - unsigned(b) + K);
    else y <= std_logic_vector(unsigned(a) + unsigned(b) + K); end if;
  end process;
end;
