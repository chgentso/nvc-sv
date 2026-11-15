library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package ev2_pkg is
   type t_chan is record
      clk_in : std_logic;
      count  : unsigned(3 downto 0);
      pulse  : std_logic;
   end record;
   type t_chans is array (natural range <>) of t_chan;
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.ev2_pkg.all;

-- Static indexes into arrays of records, vectors sized from the length of a
-- generic, and the reasons a process is approximate
entity ev_vhdl2 is
   generic (
      g_n    : natural := 2;
      g_poly : std_logic_vector := "10011" );
   port (
      clk, ext, d : in std_logic;
      cin : in std_logic_vector(g_n - 1 downto 0);
      idx : in std_logic_vector(1 downto 0);
      o : out std_logic_vector(g_poly'length - 1 downto 0);
      pulse_o : out std_logic_vector(g_n downto 0);
      q_dyn, q_loop, q_call : out std_logic );
end entity;

architecture test of ev_vhdl2 is
   constant msb : integer := g_poly'length - 1;
   signal chans : t_chans(0 to g_n);
   signal v : std_logic_vector(msb downto 0);
   signal dv : std_logic_vector(3 downto 0);

   procedure drive (signal s : out std_logic; x : in std_logic) is
   begin
      s <= x;
   end procedure;
begin
   chans(g_n).clk_in <= clk;

   gen_in: for i in 0 to g_n - 1 generate
      chans(i).clk_in <= cin(i);
   end generate;

   -- Constant indexes and a slice sized from the length of a generic
   v(0) <= clk;
   v(msb) <= ext;
   v(msb - 1 downto 1) <= (others => '0');
   o <= v;

   gen_ch: for i in 0 to g_n generate
      -- Clocked by one field of one element of an array of records
      p_count: process (chans(i).clk_in) begin
         if rising_edge(chans(i).clk_in) then
            chans(i).count <= chans(i).count + 1;
            chans(i).pulse <= not chans(i).pulse;
         end if;
      end process;
      pulse_o(i) <= chans(i).pulse;
   end generate;

   p_dyn: process (clk) begin
      if rising_edge(clk) then
         dv(to_integer(unsigned(idx))) <= d;
         q_dyn <= dv(0);
      end if;
   end process;

   p_loop: process (clk) begin
      if rising_edge(clk) then
         for i in 0 to to_integer(unsigned(idx)) loop
            q_loop <= d;
         end loop;
      end if;
   end process;

   p_call: process (clk) begin
      if rising_edge(clk) then drive(q_call, d); end if;
   end process;
end architecture;
