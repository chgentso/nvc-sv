use std.env.all;

entity env_trec_str1 is
end entity;

architecture test of env_trec_str1 is
begin
    process is
        variable tr : time_record := (
            microsecond => 500000,
            second      => 10,
            minute      => 20,
            hour        => 12,
            day         => 15,
            month       => 5,
            year        => 2025,
            weekday     => SUNDAY,
            dayofyear   => 165
        );
    begin
        assert to_string(tr) = "2025-06-15T12:20:10";
        assert to_string(tr, 2) = "2025-06-15T12:20:10.50";
        wait;
    end process;
end architecture;
