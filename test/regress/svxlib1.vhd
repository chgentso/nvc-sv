library ieee; use ieee.std_logic_1164.all; library xl; use xl.svxlib1_vcomp.all;
entity svxlib1 is end;
architecture a of svxlib1 is
  signal clk : std_logic := '0'; signal d, q : std_logic_vector(7 downto 0) := x"00";
begin
  u: svxlib1_vm generic map (W => 8) port map (clk, d, q);
  clk <= not clk after 5 ns;
  process begin d <= x"5a"; wait for 20 ns; report to_hstring(q); wait for 1 ns; std.env.finish; end process;
end;
