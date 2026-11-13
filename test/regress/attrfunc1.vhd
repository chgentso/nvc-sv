entity attrfunc1 is
    attribute tag : string;
end entity;

architecture test of attrfunc1 is
    function f1 (i : integer) return integer is
    begin
        return i + 1;
    end function;

    function f2 (i : integer) return integer is
    begin
        return i + 2;
    end function;

    procedure p1 is
    begin
    end procedure;

    attribute tag of f1, f2 : function is "ABC";
    attribute tag of p1 : procedure is "proc";
begin

    p: process is
    begin
        assert f1'tag = "ABC";
        assert f2'tag(2) = 'B';
        assert p1'tag = "proc";
        wait;
    end process;

end architecture;
