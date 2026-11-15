entity value_based is
end entity;

architecture test of value_based is
begin
    process is
    begin
        assert integer'value("16#FF#") = 255;
        assert integer'value(" -2#101#") = -5;
        assert integer'value("+7") = 7;
        assert integer'value("1E3") = 1000;
        assert integer'value("1_000") = 1000;
        assert integer'value("16#1F#E1") = 496;
        assert integer'value("-2147483648") = integer'low;
        assert time'value(" ns ") = 1 ns;
        assert time'value("16#A# ps") = 10 ps;
        assert time'value("3 us") = 3 us;
        wait;
    end process;
end architecture;
