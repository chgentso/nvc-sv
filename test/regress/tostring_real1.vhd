entity tostring_real1 is
end entity;

architecture test of tostring_real1 is
begin
    process is
    begin
        assert to_string(0.0) = "0.0" report "got " & to_string(0.0);
        assert to_string(4.0) = "4.0" report "got " & to_string(4.0);
        assert to_string(-2.0) = "-2.0" report "got " & to_string(-2.0);
        assert to_string(1500.0) = "1500.0" report "got " & to_string(1500.0);
        assert to_string(-0.5) = "-0.5" report "got " & to_string(-0.5);
        wait;
    end process;
end architecture;
