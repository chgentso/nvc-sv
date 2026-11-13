library ieee; use ieee.std_logic_1164.all;
entity fsm3 is port (clk, rst, go : in std_logic); end entity;
architecture rtl of fsm3 is
  constant S_IDLE : std_logic_vector(1 downto 0) := "00";
  constant S_RUN  : std_logic_vector(1 downto 0) := "01";
  constant S_DONE : std_logic_vector(1 downto 0) := "10";
  signal st : std_logic_vector(1 downto 0);
begin
  process (clk) begin
    if rising_edge(clk) then
      if rst = '1' then st <= S_IDLE; else
      case st is
        when S_IDLE => if go = '1' then st <= S_RUN; end if;
        when S_RUN => st <= S_DONE;
        when others => st <= S_IDLE;
      end case; end if;
    end if;
  end process;
end architecture;
