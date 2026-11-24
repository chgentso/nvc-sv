-- Exact integer exponents in IEEE.MATH_REAL "**" (both constant folding
-- and runtime evaluation).
library ieee;
use ieee.math_real.all;

entity math_pow_int1 is
end entity;

architecture test of math_pow_int1 is
    constant C_RR : real := 60.0 ** 2.0;
    constant C_IR : real := 2 ** 10.0;
    constant C_NEG : real := 2.0 ** (-3.0);
begin
    process is
        variable x : real := 60.0;
        variable y : real := 2.0;
    begin
        assert C_RR = 3600.0 report "C_RR=" & real'image(C_RR) severity failure;
        assert (3.0 ** 4.0) = 81.0 severity failure;
        assert C_IR = 1024.0 report "C_IR=" & real'image(C_IR) severity failure;
        assert C_NEG = 0.125 report "C_NEG=" & real'image(C_NEG) severity failure;
        assert (x ** y) = 3600.0 severity failure;
        wait;
    end process;
end architecture;
