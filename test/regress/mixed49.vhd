library ieee;
use ieee.std_logic_1164.all;

entity mixed49_e is
  port (p_a0, p_a1, p_ax, p_az, p_c1, p_c0, p_e1, p_e0, p_u1, p_u0,
        p_v1, p_v0, p_s1, p_s0, p_w1, p_w0 : in std_logic;
        p_vec : in std_logic_vector(3 downto 0);
        p_hi  : in std_logic_vector(1 downto 0);
        p_bit, p_plain, p_plainx : in std_logic);
end;

architecture a of mixed49_e is
begin
  process begin
    wait for 1 ns;
    assert std_logic'image(p_a0) & std_logic'image(p_a1) & std_logic'image(p_ax)
      & std_logic'image(p_az) & std_logic'image(p_c1) & std_logic'image(p_c0)
      & std_logic'image(p_e1) & std_logic'image(p_e0) & std_logic'image(p_u1)
      & std_logic'image(p_u0) & std_logic'image(p_v1) & std_logic'image(p_v0)
      & std_logic'image(p_s1) & std_logic'image(p_s0) & std_logic'image(p_w1)
      & std_logic'image(p_w0) = "'0''H''U''Z''H''L''H''L''Z''0''1''Z''1''0''1''L'"
      report "strength nets" severity failure;
    assert to_string(p_vec) = "Z1LH" and to_string(p_hi) = "Z1"
      and p_bit = 'L' and p_plain = '1' and p_plainx = 'U'
      report "vector nets" severity failure;
    wait;
  end process;
end;
