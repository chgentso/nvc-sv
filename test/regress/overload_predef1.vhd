package overload_predef1_pkg is
    type seed_t is array (1 to 2) of integer;
    function to_string(a : seed_t; sep : string := " ") return string;
end package;

package body overload_predef1_pkg is
    function to_string(a : seed_t; sep : string := " ") return string is
    begin
        return integer'image(a(1)) & sep & integer'image(a(2));
    end function;
end package body;

use work.overload_predef1_pkg.all;

entity overload_predef1 is
end entity;

architecture test of overload_predef1 is
    constant s : seed_t := (10, 20);
begin
    process is
    begin
        -- Explicit to_string with defaulted second argument must win over
        -- VHDL-2019 predefined composite to_string(seed_t) (OSVVM pattern).
        assert to_string(s) = "10 20" report "got " & to_string(s);
        assert to_string(s, ",") = "10,20" report "got " & to_string(s, ",");
        wait;
    end process;
end architecture;
