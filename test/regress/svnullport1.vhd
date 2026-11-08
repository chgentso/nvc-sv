library ieee; use ieee.std_logic_1164.all;
entity svnullport1_v is generic (W : integer := 7); port (clk : in std_logic; q : out std_logic); end;
architecture a of svnullport1_v is
  component svnullport1_vm generic (WA : integer; BW : integer); port (clk : in std_logic; we : in std_logic_vector((WA/BW)-1 downto 0); q : out std_logic); end component;
  signal wen : std_logic_vector(W/8-1 downto 0);
begin
  wen <= (others => '1');
  u: svnullport1_vm generic map (WA => W, BW => W) port map (clk, wen, q);
end;
