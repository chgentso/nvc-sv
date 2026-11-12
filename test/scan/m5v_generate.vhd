library ieee;
use ieee.std_logic_1164.all;

entity m5v_gen is
  generic (W : natural := 8; MODE : string := "fast";
           A, B : integer range 0 to 3 := 1);
  port (clk : in std_logic;
        d : in std_logic_vector(7 downto 0);
        q : out std_logic_vector(W - 1 downto 0);  -- depends on W
        m : in bit_vector(0 to 3);
        u : in std_logic_vector);
end entity;

architecture rtl of m5v_gen is
begin
  g1: for i in 0 to W - 1 generate
    u1: entity work.m5v_gen generic map (W => 4) port map (clk, d, open, m, u);
  end generate;
  g2: if W > 4 generate
    u2: entity work.m5v_gen;
  else generate
    u3: entity work.m5v_gen;
  end generate;
  g3: case W generate
    when alt1: 1 =>
      u4: entity work.m5v_gen;
    when others =>
      u5: entity work.m5v_gen;
  end generate;
  g4: if a1: W > 1 generate
    u6: entity work.m5v_gen;
  elsif a2: W > 0 generate
    u7: entity work.m5v_gen;
  end generate;
end architecture;
