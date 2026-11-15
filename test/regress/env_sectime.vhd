use std.env.all;

entity env_sectime is
end entity;

architecture test of env_sectime is
begin
    process is
        variable tm : time;
    begin
        tm := seconds_to_time(1.5);
        assert tm = 1500 ms;
        tm := seconds_to_time(-2.0);
        assert tm = -2 sec;
        tm := seconds_to_time(1.0e30);      -- Error
        assert false report "not reached";
        wait;
    end process;
end architecture;
