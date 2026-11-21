-- Generic access and file types with anonymous designated types, file
-- predefined subprograms, and generic 1D array concatenation (VHDL-2019
-- 5.8.1, 6.5.3.3, 6.5.3.4).

use std.textio.all;

package gentype10_acc is
    generic (type acc is access type is private);
    alias elem is acc'designated_subtype;
    procedure fetch(variable p : in acc; variable r : out elem);
end package;

package body gentype10_acc is
    procedure fetch(variable p : in acc; variable r : out elem) is
    begin
        r := p.all;
    end procedure;
end package body;

use std.textio.all;

package gentype10_file is
    generic (type ft is file of type is private);
    procedure try_open(st : out file_open_status);
end package;

package body gentype10_file is
    procedure try_open(st : out file_open_status) is
        file f : ft;
    begin
        file_open(st, f, "nonexistent-gentype10-dir/missing.txt", read_mode);
    end procedure;
end package body;

use std.textio.all;

entity gentype10 is
end entity;

architecture test of gentype10 is
    function concat_op generic (type v is array (natural range <>) of bit)
        parameter (a, b : v) return v is
    begin
        return a & b;
    end function;

    function concat_bv is new concat_op generic map (v => bit_vector);
    package acc_i is new work.gentype10_acc generic map (acc => line);
    package file_i is new work.gentype10_file generic map (ft => text);
begin
    check: process is
        variable l  : line := new string'("ok!");
        variable s  : string(1 to 3);
        variable st : file_open_status;
    begin
        acc_i.fetch(l, s);
        assert s = "ok!";
        deallocate(l);

        file_i.try_open(st);
        assert st = name_error;

        assert concat_bv("10", "01") = "1001";
        wait;
    end process;
end architecture;
