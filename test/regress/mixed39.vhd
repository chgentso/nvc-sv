library ieee;
use ieee.std_logic_1164.all;

package mixed39_pkg is
    type t_rec is record
        cyc : std_logic;
        dat : std_logic_vector(3 downto 0);
    end record;

    type t_rec_array is array (natural range <>) of t_rec;
end package;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.mixed39_pkg.all;

entity mixed39_sub is
    generic (n : integer := 2);
    port (o : out t_rec_array(n - 1 downto 0));
end entity;

architecture test of mixed39_sub is
begin
    gen: for i in 0 to n - 1 generate
        o(i).cyc <= '1';
        o(i).dat <= std_logic_vector(to_unsigned(i + 10, 4));
    end generate;
end architecture;
