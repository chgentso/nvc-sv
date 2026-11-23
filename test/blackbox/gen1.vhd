use work.gen1pkg.all;
entity gen1 is generic (g_init : string := ""); end entity;
architecture a of gen1 is
  signal clk, q, clk2, q2, clk3, q3 : bit;
begin
  u : brd generic map (g_sim => 1, g_init => g_init, g_iface => PLAIN) port map (clk => clk, q => q);
  u2 : entity work.brd generic map (g_sim => 1, g_init => "lit", g_iface => PLAIN) port map (clk => clk2, q => q2);
  u3 : entity work.other generic map (1, g_init, PLAIN) port map (clk => clk3, q => q3);
end architecture;
