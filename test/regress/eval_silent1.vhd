-- Regression test: a nested silent eval_try_fold triggered while initialising
-- a package during an outer silent eval_try_fold must not reset jit->silent
-- and leak diagnostics from an untaken branch.
package eval_silent1_pkg is
    function get_shift return integer;
    constant c_shift : integer;
end package;

package body eval_silent1_pkg is
    function helper(x : integer) return integer is
    begin
        return x + 1;
    end function;

    function get_shift return integer is
        constant k : integer := helper(4);
    begin
        return k - 20;
    end function;

    constant c_shift : integer := get_shift;
end package body;

library ieee;
use ieee.math_real.all;
use work.eval_silent1_pkg.all;

entity eval_silent1 is
    generic (
        scaling      : integer := -1;
        auto_scaling : integer := 1000
    );
end entity;

architecture test of eval_silent1 is
begin
    p : process is
        constant auto_mode   : integer := -1;
        constant native_mode : integer := 0;

        function pick_scale(a : integer) return integer is
        begin
            if scaling = auto_mode then
                return integer(log10(real(a))) + c_shift;
            elsif scaling = native_mode then
                return c_shift;
            else
                return integer(log10(real(scaling))) + c_shift;
            end if;
        end function;
    begin
        assert pick_scale(auto_scaling) = -12
            report "unexpected scale " & integer'image(pick_scale(auto_scaling))
            severity failure;
        wait;
    end process;
end architecture;
