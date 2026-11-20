package alias17_pkg1 is
    type color_t is (red, green, blue);
    alias hue_t is color_t;
end package;

package alias17_pkg2 is
    alias shade_t is work.alias17_pkg1.color_t;
end package;

use work.alias17_pkg1.hue_t;
use work.alias17_pkg2.shade_t;

entity alias17 is
end entity;

architecture test of alias17 is
    constant c1 : hue_t := green;
    constant c2 : shade_t := blue;
begin
    check : process is
    begin
        assert c1 = green;
        assert c2 /= red;
        assert c1 < c2;
        wait;
    end process;
end architecture;
