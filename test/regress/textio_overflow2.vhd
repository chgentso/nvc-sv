use std.textio.all;

entity textio_overflow2 is
end entity;

architecture test of textio_overflow2 is
begin
    process is
        variable l : line;
        variable i : integer;
    begin
        l := new string'("2147483648");
        read(l, i);                     -- Error
        wait;
    end process;
end architecture;
