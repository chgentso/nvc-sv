library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mixed66_mid is
  port (
    clk    : in  std_logic;
    do_frc : in  std_logic;
    do_rel : in  std_logic;
    obs    : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of mixed66_mid is
  component mixed66_leaf is
    port (
      clk : in  std_logic;
      val : out std_logic_vector(7 downto 0)
    );
  end component;

  signal leaf_val : std_logic_vector(7 downto 0);
begin
  u_leaf : mixed66_leaf port map (clk => clk, val => leaf_val);

  process(clk)
    alias sv_cnt is << signal .mixed66.u_mid.u_leaf.cnt : std_logic_vector(7 downto 0) >>;
  begin
    if rising_edge(clk) then
      obs <= sv_cnt;
      if do_frc = '1' then
        sv_cnt <= force x"AA";
      elsif do_rel = '1' then
        sv_cnt <= release;
      end if;
    end if;
  end process;
end architecture;
