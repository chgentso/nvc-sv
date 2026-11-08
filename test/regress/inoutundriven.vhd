library ieee; use ieee.std_logic_1164.all;
package p3 is
  type t_rec is record
    addr : std_logic_vector(3 downto 0);
    f    : std_logic;
  end record;
  type t_rec_arr is array (0 to 1) of std_logic_vector(1 downto 0);
end package;

library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity rd2 is port (b : inout std_logic_vector(3 downto 0); seen : out std_logic_vector(3 downto 0)); end;
architecture a of rd2 is begin seen <= b; end;
library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity rd1 is port (b : inout std_logic_vector(3 downto 0); seen : out std_logic_vector(3 downto 0)); end;
architecture a of rd1 is begin u : entity work.rd2 port map (b => b, seen => seen); end;
library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity r2 is port (r : inout t_rec; seen : out std_logic_vector(3 downto 0)); end;
architecture a of r2 is begin
  r.f <= '1';
  seen <= r.addr;
end;
library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity r1 is port (r : inout t_rec; seen : out std_logic_vector(3 downto 0)); end;
architecture a of r1 is begin
  u : entity work.r2 port map (r => r, seen => seen);
end;

library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity inoutundriven is end;
architecture t of inoutundriven is
  signal b, seen : std_logic_vector(3 downto 0);
  signal drv : std_logic_vector(3 downto 0) := "ZZZZ";
  signal r, rdrv : t_rec := (addr => "ZZZZ", f => 'Z');
  signal rseen : std_logic_vector(3 downto 0);
begin
  b <= drv;
  r <= rdrv;
  u : entity work.rd1 port map (b => b, seen => seen);
  v : entity work.r1 port map (r => r, seen => rseen);
  process begin
    wait for 1 ns;
    drv <= "0110"; rdrv.addr <= "0110";
    wait for 1 ns;
    assert seen = "0110" severity failure;
    assert rseen = "0110" severity failure;
    assert r.f = '1' severity failure;
    report "PASSED";
    wait;
  end process;
end;
