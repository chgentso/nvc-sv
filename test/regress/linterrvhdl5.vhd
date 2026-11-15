library ieee; use ieee.std_logic_1164.all;
entity e5 is port (clk, rst, go : in std_logic; busy : out std_logic); end entity;
architecture rtl of e5 is
  signal broken : integer := ;
  type st_t is (IDLE, RUN, DONE);
  signal st : st_t;
  type s2_t is (A, B);
  signal st2 : s2_t;
begin
  p1: process (clk) begin
    if rising_edge(clk) then
      if rst = '1' then st <= IDLE;
      else
        case st is
          when IDLE => if go = '1' then st <= RUN; end if;
          when RUN => st <= DONE;
          when DONE => st <= IDLE;
        end case;
      end if;
    end if;
  end process;
  p2: process (clk) begin
    if rising_edge(clk) then
      case st2 is
        when A => st2 <= ;
        when B => st2 <= A;
      end case;
    end if;
  end process;
  busy <= '1' when st = RUN else '0';
end architecture;
