-- PSL directives are cells that check and drive nothing
library ieee;
use ieee.std_logic_1164.all;

entity ev_psl is
end entity;

architecture a of ev_psl is
  signal clk, rst : std_logic;
  signal d, q, sva_q : std_logic_vector(3 downto 0);
  default clock is rising_edge(clk);
begin
  process (clk) begin
    if rising_edge(clk) then q <= d; end if;
  end process;

  process (clk) begin
    if rising_edge(clk) then sva_q <= q; end if;
  end process;

  assert always (sva_q /= "1111");
  cover {sva_q = "0001"};
  CONC : assert always (rst -> next (sva_q = "0000"));
end architecture;
