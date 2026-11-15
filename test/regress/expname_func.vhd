entity expname_func is
end entity;

architecture test of expname_func is
    type rec is record
        x : integer;
    end record;

    function f return integer is
        constant k : integer := 3;
        variable v : integer := 4;
    begin
        return f.k + f.v;
    end function;

    function g return rec is
    begin
        return (x => 7);
    end function;
begin
    process is
    begin
        assert f = 7;
        assert g.x = 7;   -- Still a call with a record result
        wait;
    end process;
end architecture;
