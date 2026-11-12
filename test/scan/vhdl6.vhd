library ieee;
use ieee.numeric_std.all;
use work.missing_pkg.all;

package vhdl6 is
    procedure check (constant addr_value : in natural;
                     constant msg        : in string);
end package;

package body vhdl6 is
    procedure check (constant addr_value : in natural;
                     constant msg        : in string) is
    begin
        -- Formal RENA does not exist and the other overloads are missing
        check(addr_value => to_unsigned(addr_value, 8),
              msg        => msg,
              rena       => msg);
    end procedure;
end package body;
