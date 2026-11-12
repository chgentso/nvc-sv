library ieee;
use ieee.std_logic_1164.all;

package m5v_p is
  type color_t is (red, green, blue);
  type int_vec is array (natural range <>) of integer;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.m5v_p.all;

entity m5v_leaf is
  generic (W : natural := 8;
           NAME : string := "def";
           R : real := 0.5;
           B : boolean := true;
           COL : color_t := green;
           V : std_logic_vector(3 downto 0) := "1010";
           T : time := 10 ns;
           C : character := 'x';
           IV : int_vec(0 to 1) := (3, 4);
           NEG : integer := -1);
  port (clk : in std_logic; d : in std_logic_vector(W - 1 downto 0));
end entity;

architecture a of m5v_leaf is
begin
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.m5v_p.all;

entity m5v_mid is
  generic (W : natural := 2; NAME : string := "mid");
  port (clk : in std_logic);
end entity;

architecture a of m5v_mid is
  signal d : std_logic_vector(W * 2 - 1 downto 0);
begin
  u: entity work.m5v_leaf
    generic map (W => W * 2, NAME => NAME & "!")
    port map (clk, d);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use work.m5v_p.all;

entity m5v_top is
end entity;

architecture a of m5v_top is
  constant K : natural := 3;
  signal clk : std_logic;
  signal d8 : std_logic_vector(7 downto 0);
  signal d5 : std_logic_vector(4 downto 0);
  component m5v_leaf is
    generic (W : natural := 1);
    port (clk : in std_logic; d : in std_logic_vector(W - 1 downto 0));
  end component;
begin
  u1: entity work.m5v_leaf port map (clk, d8);

  g: for i in 0 to 1 generate
    u2: entity work.m5v_leaf
      generic map (W => 5 + i, NAME => "x" & "y", B => false,
                   COL => blue, V => "0011", R => 1.0e-9, T => 2 us,
                   IV => (i, i + 1), NEG => -i)
      port map (clk, d5 & "0");
  end generate;

  c: if K > 2 generate
    u3: entity work.m5v_leaf generic map (W => K * 2)
      port map (clk, d8(5 downto 0));
  end generate;

  uc: component m5v_leaf generic map (W => 8) port map (clk, d8);

  um: entity work.m5v_mid generic map (W => 3) port map (clk);
end architecture;
