-- How a process uses the bits it reads
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ev_reads_v is
end entity;

architecture a of ev_reads_v is
  signal clk, s : std_logic;
  signal a, b, c, d, e, f, g, h : std_logic_vector(3 downto 0);
  signal idx : std_logic_vector(1 downto 0);
  signal y1, y2, y3, y4, y5, y6, y7 : std_logic_vector(3 downto 0);
  signal y8 : std_logic_vector(7 downto 0);
  signal y9 : std_logic;
  type mem_t is array (0 to 3) of std_logic_vector(3 downto 0);
  signal mem : mem_t;
begin
  -- A copy, and the same word as an operand of an addition
  process (clk) begin
    if rising_edge(clk) then y1 <= a; end if;
  end process;

  process (clk) begin
    if rising_edge(clk) then
      y2 <= std_logic_vector(unsigned(b) + 1);
    end if;
  end process;

  -- Bitwise operators and a multiplexer
  process (clk) begin
    if rising_edge(clk) then
      y3 <= (c and d) xor not e;
      if s = '1' then y4 <= f; else y4 <= g; end if;
    end if;
  end process;

  -- A comparison with a constant, with another word, and a bit select
  process (clk) begin
    if rising_edge(clk) then
      if h = "0101" or unsigned(a) < unsigned(b) or e(3) = '1' then
        y9 <= '1';
      else
        y9 <= '0';
      end if;
    end if;
  end process;

  -- An index
  process (clk) begin
    if rising_edge(clk) then
      y5 <= mem(to_integer(unsigned(idx)));
    end if;
  end process;

  -- A word stored concatenated
  process (clk) begin
    if rising_edge(clk) then y8 <= c & d; end if;
  end process;

  -- A word through a variable that is then added
  process (f, g)
    variable t : std_logic_vector(3 downto 0);
  begin
    t := f;
    y7 <= std_logic_vector(unsigned(t) + unsigned(g));
  end process;
end architecture;
