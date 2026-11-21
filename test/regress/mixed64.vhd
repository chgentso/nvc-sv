library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed64_vhdl is
  generic (ADD : integer := 1);
  port (
    a : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of mixed64_vhdl is
begin
  y <= std_logic_vector(unsigned(a) + to_unsigned(ADD * 10, 8));
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed64_top is
end entity;

architecture tb of mixed64_top is
  component worker_c is
    generic (ADD : integer := 1);
    port (
      a : in  std_logic_vector(7 downto 0);
      y : out std_logic_vector(7 downto 0)
    );
  end component;

  for u_spec : worker_c use entity work.mixed64_sv;

  signal a      : std_logic_vector(7 downto 0) := x"05";
  signal y_cfg  : std_logic_vector(7 downto 0);
  signal y_spec : std_logic_vector(7 downto 0);
  signal y_vhdl : std_logic_vector(7 downto 0);
begin
  u_cfg  : worker_c generic map (ADD => 3) port map (a => a, y => y_cfg);
  u_spec : worker_c generic map (ADD => 4) port map (a => a, y => y_spec);
  u_vhdl : worker_c generic map (ADD => 2) port map (a => a, y => y_vhdl);

  process
  begin
    wait for 5 ns;
    assert unsigned(y_cfg)  = 8  report "y_cfg="  & integer'image(to_integer(unsigned(y_cfg)))  severity failure;
    assert unsigned(y_spec) = 9  report "y_spec=" & integer'image(to_integer(unsigned(y_spec))) severity failure;
    assert unsigned(y_vhdl) = 25 report "y_vhdl=" & integer'image(to_integer(unsigned(y_vhdl))) severity failure;
    report "PASSED";
    wait;
  end process;
end architecture;

configuration mixed64 of mixed64_top is
  for tb
    for u_cfg  : worker_c use entity work.mixed64_sv; end for;
    for u_vhdl : worker_c use entity work.mixed64_vhdl(rtl); end for;
  end for;
end configuration;
