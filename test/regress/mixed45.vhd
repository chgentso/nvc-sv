library ieee; use ieee.std_logic_1164.all;
entity mixed45 is end;
architecture a of mixed45 is
  constant g_btp : string := "6.0";
  signal clk : std_logic := '0';
  signal q : std_logic_vector(7 downto 0);
  component mixed45_v is
    generic (BTP_N : string; BTP_O : string; CDR_N : integer; EBA_RESET : std_logic_vector(31 downto 0));
    port (clk : in std_logic; q : out std_logic_vector(7 downto 0));
  end component;
begin
  u: mixed45_v generic map (BTP_N => g_btp, BTP_O => "2.0", CDR_N => 7, EBA_RESET => x"00001000") port map (clk, q);
  process begin wait for 5 ns; assert q = x"5a" report "bad q" severity failure; wait; end process;
end;
