entity phys1 is
end entity;

architecture test of phys1 is
    type frequency is range -2147483647 to 2147483647
        units
            kHz;
            MHz = 1000 kHz;
            GHz = 1000 MHz;
        end units;
begin
    process is
        variable s : string(1 to 9) := "2.345 Mhz";
    begin
        assert time'value("0.5 ns") = 500 ps severity failure;
        assert time'value("  1.25 us ") = 1250 ns severity failure;
        assert time'value("-0.5 ns") = -500 ps severity failure;
        assert time'value("1e-3 ms") = 1 us severity failure;
        assert time'value("1.5e2 ps") = 150 ps severity failure;
        assert time'value("1_000.5 ns") = 1000500 ps severity failure;
        assert time'value("ns") = 1 ns severity failure;
        assert frequency'value("2.345 Mhz") = 2345 kHz severity failure;
        assert frequency'value(s) = 2345 kHz severity failure;
        assert frequency'value("2000 khz") = 2 MHz severity failure;
        wait;
    end process;
end architecture;
