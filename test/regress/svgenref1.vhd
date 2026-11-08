library ieee;
use ieee.std_logic_1164.all;

entity svgenref1 is
end entity;

architecture test of svgenref1 is
  component svgenref1_sub is
    generic (EN : integer := 1);
    port (clk : in std_logic;
          d   : in std_logic_vector(3 downto 0);
          q   : out std_logic_vector(3 downto 0));
  end component;
  signal clk : std_logic := '0';
  signal d, q : std_logic_vector(3 downto 0) := "0000";
begin
  u: svgenref1_sub port map (clk, d, q);

  process is
  begin
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    assert q = "0000" report "q=" & to_string(q) severity failure;
    clk <= '0';
    d <= "0011";
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    assert q = "UUUU" report "q=" & to_string(q) severity failure;
    wait;
  end process;
end architecture;
