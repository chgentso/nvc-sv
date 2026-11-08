use std.textio.all;

entity file87 is
end entity;

architecture test of file87 is
    procedure rd1 (variable s : in text; variable n : out integer) is
        variable l : line;
    begin
        readline(s, l);
        read(l, n);
    end procedure;

    procedure rd2 (s : in text; variable n : out integer) is
        variable l : line;
    begin
        readline(s, l);
        read(l, n);
    end procedure;
begin
    process is
        file f : text open write_mode is "file87.dat";
        variable l : line;
    begin
        write(l, 42);
        writeline(f, l);
        write(l, 43);
        writeline(f, l);
        file_close(f);
        wait;
    end process;

    process is
        file f : text;
        variable n : integer;
    begin
        wait for 1 ns;
        file_open(f, "file87.dat", read_mode);
        rd1(f, n);
        assert n = 42;
        rd2(f, n);
        assert n = 43;
        wait;
    end process;
end architecture;
