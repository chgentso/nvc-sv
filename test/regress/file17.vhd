use std.textio.all;

entity file17 is
end entity;

architecture test of file17 is
    type int_file is file of integer;
begin
    process is
        file f : int_file;
        variable v : integer;
    begin
        assert not file_canseek(output);

        file_open(f, "file17.dat", WRITE_MODE);
        for i in 0 to 4 loop
            write(f, i * 10);
        end loop;
        file_close(f);

        file_open(f, "file17.dat", READ_WRITE_MODE);
        assert file_canseek(f);
        assert file_size(f) = 5;

        file_seek(f, 3);
        read(f, v);
        assert v = 30;
        assert file_position(f) = 4;

        file_rewind(f);
        assert file_position(f) = 0;
        read(f, v);
        assert v = 0;

        file_seek(f, 3, FILE_ORIGIN_CURRENT);
        assert file_position(f) = 4;
        read(f, v);
        assert v = 40;

        file_truncate(f, 3);
        assert file_size(f) = 3;

        file_close(f);
        wait;
    end process;
end architecture;
