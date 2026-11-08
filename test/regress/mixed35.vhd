package mixed35_pkg is
    type mode_t is (CLASSIC, PIPELINED, BURST);
end package;

-------------------------------------------------------------------------------

use work.mixed35_pkg.all;

entity mixed35_sub is
    generic (g : mode_t := CLASSIC);
    port (o : out integer);
end entity;

architecture test of mixed35_sub is
begin
    o <= mode_t'pos(g);
end architecture;
