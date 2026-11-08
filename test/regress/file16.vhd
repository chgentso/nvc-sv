-- LCS-2016-014: composites of file types.  Each noncomposite subelement of
-- a composite file object is an independent file (IEEE 1076-2019 5.3.1,
-- 6.4.2.5, 6.5.2, 4.2.2.4).

entity file16 is
end entity;

architecture test of file16 is
    type int_file is file of integer;
    type int_file_vec is array (1 to 3) of int_file;
    type file_rec is record
        data : int_file;
        log  : int_file;
    end record;

    procedure write_all (file fv : int_file_vec; base : integer) is
    begin
        for i in fv'range loop
            for j in 1 to i loop
                write(fv(i), base * i + j);
            end loop;
        end loop;
    end procedure;

    procedure check_all (file fv : int_file_vec; base : integer) is
        variable x : integer;
    begin
        for i in fv'range loop
            for j in 1 to i loop
                assert not endfile(fv(i));
                read(fv(i), x);
                assert x = base * i + j
                    report "file " & integer'image(i) & " got "
                    & integer'image(x);
            end loop;
            assert endfile(fv(i));
        end loop;
    end procedure;
begin

    process is
        file fv : int_file_vec;
        file fr : file_rec;
        variable status : file_open_status;
        variable x : integer;
    begin
        for i in fv'range loop
            file_open(status, fv(i), "file16_" & integer'image(i) & ".bin",
                      write_mode);
            assert status = open_ok;
        end loop;

        write_all(fv, 100);

        for i in fv'range loop
            file_close(fv(i));
        end loop;

        for i in fv'range loop
            file_open(fv(i), "file16_" & integer'image(i) & ".bin",
                      read_mode);
        end loop;

        check_all(fv, 100);

        for i in fv'range loop
            file_close(fv(i));
        end loop;

        -- Record of files: the two elements are independent
        file_open(fr.data, "file16_data.bin", write_mode);
        file_open(fr.log, "file16_log.bin", write_mode);
        write(fr.data, 1);
        write(fr.log, 2);
        write(fr.data, 3);
        file_close(fr.data);
        file_close(fr.log);

        file_open(fr.data, "file16_data.bin", read_mode);
        file_open(fr.log, "file16_log.bin", read_mode);
        read(fr.data, x);
        assert x = 1;
        read(fr.data, x);
        assert x = 3;
        assert endfile(fr.data);
        read(fr.log, x);
        assert x = 2;
        assert endfile(fr.log);
        file_close(fr.data);
        file_close(fr.log);

        wait;
    end process;

end architecture;
