library ieee;
use ieee.math_real.all;
use std.env.all;

entity math_domain is
end entity;

architecture test of math_domain is
begin
    process is
        variable r : real;
    begin
        -- Exact results prescribed at multiples of pi
        assert sin(math_pi) = 0.0;
        assert sin(-math_pi) = 0.0;
        assert sin(math_2_pi) = 0.0;
        assert cos(math_pi_over_2) = 0.0;
        assert cos(math_3_pi_over_2) = 0.0;
        assert tan(math_pi) = 0.0;
        assert tan(math_2_pi) = 0.0;

        -- Calls outside the domain report an error and return a value
        SetVhdlAssertEnable(ERROR, FALSE);
        r := log(0.0);
        assert r = real'low;
        r := log2(-1.0);
        assert r = real'low;
        r := log10(0.0);
        assert r = real'low;
        r := (-2.0) ** 0.5;
        assert r = 0.0;
        r := 0.0 ** (-1.0);
        assert r = 0.0;
        SetVhdlAssertEnable(ERROR, TRUE);

        assert abs(log(math_e) - 1.0) < 1.0e-12;
        assert abs(2.0 ** 10.0 - 1024.0) < 1.0e-9;
        wait;
    end process;
end architecture;
