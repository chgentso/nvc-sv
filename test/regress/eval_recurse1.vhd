package eval_recurse1_pkg is
    type small_t is range 0 to 3;
    function "="(a, b : small_t) return boolean;
end package;

package body eval_recurse1_pkg is
    function "="(a, b : small_t) return boolean is
    begin
        return a = b;
    end function;
end package body;

use work.eval_recurse1_pkg.all;

entity eval_recurse1 is
begin
    assert small_t'(1) = small_t'(0) severity note;
end entity;

architecture test of eval_recurse1 is
begin
end architecture;
