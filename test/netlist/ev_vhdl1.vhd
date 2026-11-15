library ieee;
use ieee.std_logic_1164.all;

package ev_pkg is
   procedure pulse (signal s : out std_logic; v : in std_logic);
end package;

package body ev_pkg is
   procedure pulse (signal s : out std_logic; v : in std_logic) is
   begin
      s <= v;
   end procedure;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use work.ev_pkg.all;

entity ev_vhdl1 is
   port (
      clk, clk2 : in std_logic;
      rst_n, rst, set_n, en : in std_logic;
      d, d2, a, b : in std_logic;
      dv : in std_logic_vector(7 downto 0);
      sel : in std_logic_vector(1 downto 0);
      q_rise, q_fall, q_event, q_wait : out std_logic;
      q_arl, q_arh, q_ars, q_srst, q_en : out std_logic;
      s0, s1 : out std_logic;
      qv : out std_logic_vector(7 downto 0);
      q_logic, q_tmp, q_reg, qa, qb : out std_logic;
      c_mix, q_mix : out std_logic;
      q_clk, c_copy, c_expr, c_proc, c_proc2 : out std_logic;
      q_case, q_gen, q_other, q_loop, q_clkand, q_ddr : out std_logic );
   attribute keep : boolean;
   attribute keep of q_rise : signal is true;
   attribute async_reg : string;
   attribute async_reg of s0, s1 : signal is "true";
   attribute shreg_extract : string;
   attribute shreg_extract of s0 : signal is "no";
   attribute cdc_depth : integer;
   attribute cdc_depth of s1 : signal is 2;
   attribute cdc_domain : string;
   attribute cdc_domain of ev_vhdl1 : entity is "clk";
end entity;

architecture test of ev_vhdl1 is
   attribute keep_hierarchy : string;
   attribute keep_hierarchy of test : architecture is "true";
   signal t0, t1 : std_logic;
   attribute dont_touch : string;
   attribute dont_touch of t0 : signal is "yes";
   attribute dont_touch of others : signal is "no";
   signal gen_q : std_logic_vector(1 downto 0);
begin
   p_rise: process (clk) begin
      if rising_edge(clk) then q_rise <= d; end if;
   end process;

   p_fall: process (clk) begin
      if falling_edge(clk) then q_fall <= d; end if;
   end process;

   p_event: process (clk) begin
      if clk'event and clk = '1' then q_event <= d; end if;
   end process;

   p_wait: process begin
      wait until rising_edge(clk);
      q_wait <= d;
   end process;

   p_arl: process (clk, rst_n) begin
      if rst_n = '0' then q_arl <= '0';
      elsif rising_edge(clk) then q_arl <= d; end if;
   end process;

   p_arh: process (clk, rst) begin
      if rst = '1' then q_arh <= '0';
      elsif rising_edge(clk) then q_arh <= d; end if;
   end process;

   p_ars: process (clk, set_n) begin
      if set_n = '0' then q_ars <= '1';
      elsif rising_edge(clk) then q_ars <= d; end if;
   end process;

   p_srst: process (clk) begin
      if rising_edge(clk) then
         if rst = '1' then q_srst <= '0'; else q_srst <= d; end if;
      end if;
   end process;

   p_en: process (clk) begin
      if rising_edge(clk) then
         if en = '1' then q_en <= d; end if;
      end if;
   end process;

   p_chain: process (clk, rst_n) begin
      if rst_n = '0' then s0 <= '0'; s1 <= '0';
      elsif rising_edge(clk) then s0 <= d; s1 <= s0; end if;
   end process;

   p_slice: process (clk) begin
      if rising_edge(clk) then qv(3 downto 0) <= dv(3 downto 0); end if;
   end process;

   p_logic: process (clk) begin
      if rising_edge(clk) then q_logic <= a and b; end if;
   end process;

   p_tmp: process (clk)
      variable t : std_logic;
   begin
      if rising_edge(clk) then t := a and b; q_tmp <= t; end if;
   end process;

   p_reg: process (clk)
      variable r : std_logic;
   begin
      if rising_edge(clk) then q_reg <= r; r := d; end if;
   end process;

   p_two: process (clk, clk2) begin
      if rising_edge(clk) then qa <= d; end if;
      if rising_edge(clk2) then qb <= d2; end if;
   end process;

   p_mix: process (clk, a) begin
      c_mix <= a;
      if rising_edge(clk) then q_mix <= d; end if;
   end process;

   p_clk: process (clk) begin
      if rising_edge(clk) then q_clk <= clk; end if;
   end process;

   c_copy <= d;
   c_expr <= a and b;

   p_proc: process (clk) begin
      if rising_edge(clk) then pulse(c_proc, d); end if;
   end process;

   p_comb: process (a, b) begin
      c_proc2 <= a or b;
   end process;

   p_case: process (clk) begin
      if rising_edge(clk) then
         case sel is
            when "00" => q_case <= a;
            when "01" => q_case <= b;
            when others => q_case <= '0';
         end case;
      end if;
   end process;

   gen: for i in 0 to 1 generate
      p_g: process (clk) begin
         if rising_edge(clk) then gen_q(i) <= dv(i); end if;
      end process;
   end generate;
   q_gen <= gen_q(0) xor gen_q(1);

   p_other: process begin
      q_other <= '0';
      wait for 10 ns;
      q_other <= '1';
      wait for 10 ns;
   end process;

   p_loop: process (clk)
      variable acc : std_logic;
   begin
      if rising_edge(clk) then
         acc := '0';
         for i in dv'range loop
            acc := acc xor dv(i);
         end loop;
         q_loop <= acc;
      end if;
   end process;

   p_ddr: process (clk) begin
      if rising_edge(clk) then q_ddr <= d;
      elsif falling_edge(clk) then q_ddr <= d2; end if;
   end process;

   p_clkand: process (clk) begin
      if rising_edge(clk) and en = '1' then q_clkand <= d; end if;
   end process;
end architecture;
