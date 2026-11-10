library lib_a, lib_b, lib_c;
use lib_a.pkg_a.all;
use lib_b.pkg_b.all;
use lib_c.pkg_c.item;

package p is
    constant c : integer := lib_a.pkg_d.value;
end package;
