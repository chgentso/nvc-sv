library ieee; use ieee.std_logic_1164.all;
entity fsm5b is port (clk, rst, go, fin, abort : in std_logic; busy : out std_logic); end entity;
architecture rtl of fsm5b is
  signal bad : ;
  type st_t is (IDLE, RUN, DONE);
  signal state, nxt : st_t;
  signal s1 : st_t := IDLE;
begin
  p_reg: process (clk, rst) begin
    if rst = '1' then state <= IDLE;
    elsif rising_edge(clk) then state <= nxt; end if;
  end process;
  p_next: process (all) begin
    nxt <= state;
    case state is
      when IDLE => if go = '1' then nxt <= RUN; end if;
      when RUN =>
        if fin = '1' and
           go = '0' then nxt <= DONE;
        elsif abort = '1' then nxt <= IDLE; end if;
      when others => nxt <= IDLE;
    end case;
  end process;
  p_one: process (clk) begin
    if rising_edge(clk) then
      if rst = '1' then s1 <= IDLE;
      else
        case s1 is
          when IDLE | DONE => s1 <= RUN;
          when RUN => s1 <= DONE when go = '1' else RUN;
        end case;
      end if;
    end if;
  end process;
end architecture;
