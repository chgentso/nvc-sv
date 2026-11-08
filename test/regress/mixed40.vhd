library ieee;
use ieee.std_logic_1164.all;

package mixed40_pkg is
    type t_rec is record
        ack   : std_logic;
        stall : std_logic;
        err   : std_logic;
    end record;
end package;
