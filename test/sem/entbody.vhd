entity entbody is
    function f1 (x : integer) return integer;  -- Error
    function f2 (x : integer) return integer;  -- OK
    function f2 (x : integer) return integer is
    begin
        return x + 1;
    end function;
    procedure p1 (x : integer);  -- Error
end entity;
