package overload_predef1_pkg is
    type my_real is range 0.0 to 100.0;
    function to_string(v : my_real; unit : string := "") return string;
end package;

package body overload_predef1_pkg is
    function to_string(v : my_real; unit : string := "") return string is
    begin
        return real'image(real(v)) & unit;
    end function;
end package body;

use work.overload_predef1_pkg.all;

entity overload_predef1 is
end entity;

architecture test of overload_predef1 is
    constant x : my_real := 1.0;
begin
    process is
    begin
        report to_string(x);
        wait;
    end process;
end architecture;
