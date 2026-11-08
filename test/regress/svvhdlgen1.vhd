library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
entity svvhdlgen1_v is generic (n : integer := 2); port (clk : in std_logic); end;
architecture a of svvhdlgen1_v is
begin
  g : for i in 0 to n - 1 generate
    signal cnt : std_logic_vector(2 downto 0) := "000";
  begin
    process (clk) begin
      if rising_edge(clk) then cnt <= std_logic_vector(unsigned(cnt) + 1 + i); end if;
    end process;
  end generate g;
end;
