library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity mixed43_e is port (clk : in std_logic; q : out std_logic_vector(3 downto 0)); end;
architecture a of mixed43_e is
  signal cnt : std_logic_vector(3 downto 0) := "0000";
  signal flag : std_logic := '0';
begin
  process (clk) begin
    if rising_edge(clk) then cnt <= std_logic_vector(to_unsigned(1,4) + unsigned(cnt)) ; end if;
  end process;
  q <= cnt;
  flag <= '1' when cnt = "0100" else '0';
end;
