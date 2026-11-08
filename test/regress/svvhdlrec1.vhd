library ieee;
use ieee.std_logic_1164.all;
package svvhdlrec1_pkg is
  type t_wb_out is record
    ack   : std_logic;
    err   : std_logic;
    dat   : std_logic_vector(7 downto 0);
  end record;
  type t_wb_in is record
    cyc : std_logic;
    adr : std_logic_vector(3 downto 0);
  end record;
end package;
library ieee;
use ieee.std_logic_1164.all;
use work.svvhdlrec1_pkg.all;
entity svvhdlrec1_dut is
  port (i : in t_wb_out; o : out t_wb_in);
end;
architecture a of svvhdlrec1_dut is
begin
  o.cyc <= i.ack;
  o.adr <= i.dat(3 downto 0);
end;
