library ieee; use ieee.std_logic_1164.all;
use work.fsmpkg.all;
entity fsm6 is port (clk, rst, go, fin : in std_logic); end entity;
architecture rtl of fsm6 is
  signal state : st_t;
  signal other : st_t;
begin
  process (clk) begin
    if rising_edge(clk) then
      if rst = '1' then state <= IDLE;
      else
        case state is
          when IDLE => if go = '1' then state <= RUN; end if;
          when RUN => if fin = '1' then state <= DONE; else state <= other; end if;
          when others => state <= IDLE;
        end case;
      end if;
    end if;
  end process;
end architecture;
