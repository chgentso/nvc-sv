entity enumhomo is
end entity;

architecture test of enumhomo is
    type enu is (one, two);
    type enu2 is (red, green);

    function one return enu is  -- Error
    begin
        return two;
    end function;

    function two (x : integer) return enu is  -- OK
    begin
        return one;
    end function;

    function red return integer is  -- OK
    begin
        return 1;
    end function;

    function green return enu2 is  -- Error
    begin
        return red;
    end function;
begin
    process is
        function one return enu is  -- OK, inner region
        begin
            return two;
        end function;
    begin
        wait;
    end process;
end architecture;
