library ieee; use ieee.std_logic_1164.all;
entity fsm2 is port (clk, rst, go : in std_logic); end entity;
architecture rtl of fsm2 is
  type st_t is (IDLE, RUN, DONE);
  type reg_t is record state : st_t; cnt : integer; end record;
  signal r, rin : reg_t;
  signal ctrl, ctrl_nxt : reg_t;
begin
  seq: process (clk) begin
    if rising_edge(clk) then
      if rst = '1' then r.state <= IDLE; else r <= rin; end if;
    end if;
  end process;
  comb: process (all)
    variable v : reg_t;
  begin
    v := r;
    case r.state is
      when IDLE => if go = '1' then v.state := RUN; end if;
      when RUN => v.state := DONE;
      when DONE => v.state := IDLE;
    end case;
    rin <= v;
  end process;
  seq2: process (clk) begin
    if rising_edge(clk) then ctrl <= ctrl_nxt; end if;
  end process;
  comb2: process (all) begin
    ctrl_nxt <= ctrl;
    case ctrl.state is
      when IDLE => ctrl_nxt.state <= RUN;
      when others => ctrl_nxt.state <= IDLE;
    end case;
  end process;
end architecture;
