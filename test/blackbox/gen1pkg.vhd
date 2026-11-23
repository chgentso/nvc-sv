package gen1pkg is
  type t_iface is (PLAIN, STREAMERS);
  component brd is
    generic (g_sim : integer := 0; g_iface : t_iface := plain; g_init : string := "def");
    port (clk : in bit; q : out bit);
  end component;
end package;
