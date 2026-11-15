entity toggle_stage is
  port (
    clk  : in  bit;
    en   : in  boolean;
    q    : out bit := '0'
  );
end entity;

architecture rtl of toggle_stage is
begin

  -- the "if" after the label is missing
  gen_run : en generate
    type mode_t is (idle, running, done);
    signal mode : mode_t := idle;
    signal flag : bit := '0';
  begin

    q <= flag;

    fsm : process
    begin
      wait until clk = '1';
      case mode is
        when idle =>
          mode <= running;
        when running =>
          flag <= not flag;
          mode <= done;
        when done =>
          mode <= idle;
      end case;
    end process;

  end generate;

end architecture;
