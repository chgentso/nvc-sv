entity file_readeof is
end entity;

architecture test of file_readeof is
    type int_file is file of integer;
begin
    process is
        file f : int_file;
        variable v : integer;
    begin
        file_open(f, "file_readeof.tmp", write_mode);
        write(f, 1);
        write(f, 2);
        file_close(f);
        file_open(f, "file_readeof.tmp", read_mode);
        read(f, v);
        assert v = 1;
        read(f, v);
        assert v = 2;
        assert endfile(f);
        read(f, v);                     -- Error
        assert false report "not reached";
        wait;
    end process;
end architecture;
