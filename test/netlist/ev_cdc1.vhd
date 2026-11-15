-- Own stand-ins for the usual clock domain crossing building blocks, each
-- a unit of the name the blocks have, with the structure and attributes
-- that a clock domain analysis has to recognise

library ieee;
use ieee.std_logic_1164.all;

entity gc_sync is
   port (
      clk_i     : in  std_logic;
      rst_n_a_i : in  std_logic;
      d_i       : in  std_logic;
      q_o       : out std_logic );
end entity;

architecture arch of gc_sync is
   signal sync0, sync1 : std_logic;
   attribute keep_hierarchy : string;
   attribute keep_hierarchy of arch : architecture is "true";
   attribute async_reg : string;
   attribute async_reg of sync0 : signal is "true";
   attribute async_reg of sync1 : signal is "true";
   attribute shreg_extract : string;
   attribute shreg_extract of sync0 : signal is "no";
begin
   process (clk_i, rst_n_a_i) begin
      if rst_n_a_i = '0' then
         sync1 <= '0';
         sync0 <= '0';
      elsif rising_edge(clk_i) then
         sync0 <= d_i;
         sync1 <= sync0;
      end if;
   end process;

   q_o <= sync1;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity gc_sync_ffs is
   port (
      clk_i    : in  std_logic;
      rst_n_i  : in  std_logic;
      data_i   : in  std_logic;
      synced_o : out std_logic;
      ppulse_o : out std_logic );
end entity;

architecture arch of gc_sync_ffs is
   signal sync, sync_d : std_logic;
begin
   cmp_gc_sync : entity work.gc_sync
      port map (clk_i => clk_i, rst_n_a_i => rst_n_i, d_i => data_i,
                q_o => sync);

   process (clk_i, rst_n_i) begin
      if rst_n_i = '0' then
         synced_o <= '0';
         sync_d <= '0';
         ppulse_o <= '0';
      elsif rising_edge(clk_i) then
         synced_o <= sync;
         sync_d <= sync;
         ppulse_o <= sync and not sync_d;
      end if;
   end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- Toggle in the source domain, synchronised, edge detected in the other
entity gc_pulse_synchronizer is
   port (
      clk_in_i    : in  std_logic;
      rst_in_n_i  : in  std_logic;
      clk_out_i   : in  std_logic;
      rst_out_n_i : in  std_logic;
      d_ready_o   : out std_logic;
      d_p_i       : in  std_logic;
      q_p_o       : out std_logic );
end entity;

architecture rtl of gc_pulse_synchronizer is
   signal toggle, tsync, tprev : std_logic;
begin
   process (clk_in_i, rst_in_n_i) begin
      if rst_in_n_i = '0' then
         toggle <= '0';
      elsif rising_edge(clk_in_i) then
         if d_p_i = '1' then
            toggle <= not toggle;
         end if;
      end if;
   end process;

   cmp_sync : entity work.gc_sync
      port map (clk_i => clk_out_i, rst_n_a_i => rst_out_n_i, d_i => toggle,
                q_o => tsync);

   process (clk_out_i, rst_out_n_i) begin
      if rst_out_n_i = '0' then
         tprev <= '0';
         q_p_o <= '0';
      elsif rising_edge(clk_out_i) then
         tprev <= tsync;
         q_p_o <= tsync xor tprev;
      end if;
   end process;

   d_ready_o <= '1';
end architecture;

library ieee;
use ieee.std_logic_1164.all;

-- A word held in the source domain and a request toggled through the
-- synchroniser: the word is only sampled when the request is seen
entity gc_sync_word_wr is
   port (
      clk_in_i    : in  std_logic;
      rst_n_i     : in  std_logic;
      clk_out_i   : in  std_logic;
      data_in_i   : in  std_logic_vector(7 downto 0);
      data_out_o  : out std_logic_vector(7 downto 0) );
end entity;

architecture rtl of gc_sync_word_wr is
   signal hold : std_logic_vector(7 downto 0);
   signal req, req_sync, req_prev : std_logic;
begin
   process (clk_in_i) begin
      if rising_edge(clk_in_i) then
         hold <= data_in_i;
         req <= not req;
      end if;
   end process;

   cmp_sync : entity work.gc_sync
      port map (clk_i => clk_out_i, rst_n_a_i => rst_n_i, d_i => req,
                q_o => req_sync);

   process (clk_out_i) begin
      if rising_edge(clk_out_i) then
         req_prev <= req_sync;
         if req_sync /= req_prev then
            data_out_o <= hold;
         end if;
      end if;
   end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity ev_cdc1 is
   port (
      clk_a_i, clk_b_i : in std_logic;
      rst_a_n_i, rst_b_n_i : in std_logic;
      irq_a_i : in std_logic;
      pulse_a_i : in std_logic;
      word_a_i : in std_logic_vector(7 downto 0);
      irq_b_o, irq_pulse_b_o, pulse_b_o : out std_logic;
      word_b_o : out std_logic_vector(7 downto 0);
      raw_b_o : out std_logic );
end entity;

architecture test of ev_cdc1 is
   signal irq_a, irq_a_n : std_logic;
   signal ready : std_logic;
begin
   -- Domain a
   p_irq_a: process (clk_a_i, rst_a_n_i) begin
      if rst_a_n_i = '0' then
         irq_a <= '0';
      elsif rising_edge(clk_a_i) then
         irq_a <= irq_a_i;
      end if;
   end process;

   -- Crossing a to b through a synchroniser
   u_sync: entity work.gc_sync_ffs
      port map (clk_i => clk_b_i, rst_n_i => rst_b_n_i, data_i => irq_a,
                synced_o => irq_b_o, ppulse_o => irq_pulse_b_o);

   u_pulse: entity work.gc_pulse_synchronizer
      port map (clk_in_i => clk_a_i, rst_in_n_i => rst_a_n_i,
                clk_out_i => clk_b_i, rst_out_n_i => rst_b_n_i,
                d_ready_o => ready, d_p_i => pulse_a_i, q_p_o => pulse_b_o);

   u_word: entity work.gc_sync_word_wr
      port map (clk_in_i => clk_a_i, rst_n_i => rst_b_n_i,
                clk_out_i => clk_b_i, data_in_i => word_a_i,
                data_out_o => word_b_o);

   -- A crossing without a synchroniser
   p_raw_b: process (clk_b_i) begin
      if rising_edge(clk_b_i) then
         raw_b_o <= irq_a;
      end if;
   end process;
end architecture;
