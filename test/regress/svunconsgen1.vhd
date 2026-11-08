library ieee; use ieee.std_logic_1164.all;
entity svunconsgen1_lfsr is
  generic (g_length : integer; g_init_value : std_logic_vector; g_taps : std_logic_vector);
  port (clk_i : in std_logic; q_o : out std_logic_vector(g_length-1 downto 0));
end;
architecture rtl of svunconsgen1_lfsr is
  signal r : std_logic_vector(g_length-1 downto 0) := g_init_value;
begin
  process (clk_i) begin
    if rising_edge(clk_i) then r <= r xor g_taps; end if;
  end process;
  q_o <= r;
end;
