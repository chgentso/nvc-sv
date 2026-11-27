entity textio_noeol is
end entity;

use std.textio.all;

architecture test of textio_noeol is
begin
    process is
        file f     : text;
        variable l : line;
    begin
        -- Write two lines where the second line has no trailing newline
        file_open(f, "textio_noeol.tmp", write_mode);
        write(f, "first" & LF & "second");
        file_close(f);

        file_open(f, "textio_noeol.tmp", read_mode);
        assert not endfile(f);

        readline(f, l);
        assert l.all = "first" report "line 1 was: " & l.all;
        assert not endfile(f);

        readline(f, l);
        assert l.all = "second" report "line 2 was: " & l.all;
        assert endfile(f);

        deallocate(l);
        file_close(f);
        wait;
    end process;
end architecture;
