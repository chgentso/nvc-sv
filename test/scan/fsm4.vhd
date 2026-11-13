library ieee; use ieee.std_logic_1164.all;
use work.fsmpkg.all;
entity fsm4 is port (clk, rst, go : in std_logic; op : in op_t; sel : in std_ulogic); end entity;
architecture rtl of fsm4 is
  type st_t is (IDLE, RUN);
  signal reg_only : st_t;
  signal n1, n2 : st_t;
  signal ext, ret_state : ext_t;
  constant A : std_logic_vector(1 downto 0) := "00";
  constant B : std_logic_vector(1 downto 0) := "01";
  signal two : std_logic_vector(1 downto 0);
  signal o : std_logic;
begin
  -- Register without a case
  p1: process (clk) begin
    if rising_edge(clk) then
      if go = '1' then reg_only <= RUN; else reg_only <= IDLE; end if;
    end if;
  end process;
  -- Decoder on a port: never assigned in a clocked process
  p2: process (all) begin
    case op is
      when OP_ADD => o <= '1';
      when others => o <= '0';
    end case;
  end process;
  -- Case on the next-state signal only
  p3: process (clk) begin
    if rising_edge(clk) then n1 <= n2; end if;
  end process;
  p4: process (all) begin
    n2 <= IDLE;
    case n2 is
      when IDLE => n2 <= RUN;
      when others => n2 <= IDLE;
    end case;
  end process;
  -- Case on a standard logic subject
  p5: process (clk) begin
    if rising_edge(clk) then
      case sel is
        when '1' => o <= '1';
        when others => o <= '0';
      end case;
    end if;
  end process;
  -- Two constants and no reset
  p6: process (clk) begin
    if rising_edge(clk) then
      case two is
        when A => two <= B;
        when others => two <= A;
      end case;
    end if;
  end process;
  -- A type that comes from a package the scan does not have
  p7: process (clk) begin
    if rising_edge(clk) then
      case ext is
        when E_ONE => ext <= E_TWO;
        when E_TWO => ext <= ret_state;
        when others => ext <= E_ONE;
      end case;
    end if;
  end process;
end architecture;
