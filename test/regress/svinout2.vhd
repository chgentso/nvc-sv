library ieee; use ieee.std_logic_1164.all;
package p3 is
  type t_rec is record
    addr : std_logic_vector(3 downto 0);
    f    : std_logic;
  end record;
  type t_rec_arr is array (0 to 1) of std_logic_vector(1 downto 0);
end package;

library ieee; use ieee.std_logic_1164.all; use work.p3.all;
entity l3 is port (b : inout std_logic_vector(3 downto 0); en : in std_logic; seen : out std_logic_vector(3 downto 0)); end;
architecture a of l3 is
begin
  b <= "1100" when en = '1' else (others => 'Z');   -- VHDL driver
  seen <= b;
end;
library ieee; use ieee.std_logic_1164.all;
entity l2 is port (b : inout std_logic_vector(3 downto 0); en : in std_logic; seen : out std_logic_vector(3 downto 0)); end;
architecture a of l2 is
begin
  u : entity work.l3 port map (b => b, en => en, seen => seen);
end;
library ieee; use ieee.std_logic_1164.all;
entity l1 is port (b : inout std_logic_vector(3 downto 0); en : in std_logic; seen : out std_logic_vector(3 downto 0)); end;
architecture a of l1 is
begin
  u : entity work.l2 port map (b => b, en => en, seen => seen);
end;

library ieee; use ieee.std_logic_1164.all;
entity s2 is port (b : inout std_logic_vector(1 downto 0); seen : out std_logic_vector(1 downto 0)); end;
architecture a of s2 is begin
  b(0) <= '1' when b(1) = '1' else 'Z';
  seen <= b;
end;
library ieee; use ieee.std_logic_1164.all;
entity ls is port (b : inout std_logic_vector(3 downto 0); seen : out std_logic_vector(1 downto 0)); end;
architecture a of ls is begin
  u : entity work.s2 port map (b => b(2 downto 1), seen => seen);
end;

