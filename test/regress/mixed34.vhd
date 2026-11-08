package mixed34_pkg is
    type t_arr is array (1 to 4) of integer;
    type t_down is array (3 downto 0) of integer;
end package;

-------------------------------------------------------------------------------

use work.mixed34_pkg.all;

entity mixed34_sub is
    port (i  : in t_arr;
          o  : out t_arr;
          od : out t_down;
          io : in t_down);
end entity;

architecture test of mixed34_sub is
begin
    process (i, io) is
    begin
        o <= (i(4), i(3), i(2), i(1));
        od <= (io(0), io(1), io(2), io(3) + 1);
    end process;
end architecture;
