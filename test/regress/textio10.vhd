use std.textio.all;

entity textio10 is
end entity;

architecture test of textio10 is
begin
    process is
        variable l : line;
        variable a, b : real;
    begin
        a := 5.0 / 7.0;
        write(l, a);
        read(l, b);
        assert a = b report real'image(a) & " /= " & real'image(b)
            severity failure;
        deallocate(l);

        a := -1.0 / 3.0;
        write(l, a);
        read(l, b);
        assert a = b severity failure;
        deallocate(l);
        wait;
    end process;
end architecture;
