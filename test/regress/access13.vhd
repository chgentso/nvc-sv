entity access13 is
end entity;

architecture test of access13 is
    type str_ptr is access string;

    procedure fill(variable s : inout string; c : character) is
    begin
        s := (s'range => c);
    end procedure;

    procedure overwrite(variable p : in str_ptr) is
    begin
        p.all := (p'range => 'b');
        assert p.all = "bbbb";
        p(2) := 'x';
        assert p.all = "bxbb";
        fill(p.all, 'z');
        assert p.all = "zzzz";
    end procedure;
begin
    check : process is
        variable buf : str_ptr := new string'("1234");
    begin
        overwrite(buf);
        assert buf.all = "zzzz";
        deallocate(buf);
        wait;
    end process;
end architecture;
