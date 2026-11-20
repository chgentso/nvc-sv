package attr28_pkg is
    generic (n : integer := 1);

    function get_n return integer;
    attribute tag : string;
    attribute tag of get_n : function is "pkg_func";
end package;

package body attr28_pkg is
    function get_n return integer is
    begin
        return n;
    end function;
end package body;

entity attr28 is
end entity;

architecture test of attr28 is
    package p is new work.attr28_pkg generic map (n => 42);
begin
    check : process is
    begin
        assert p.get_n'tag = "pkg_func";
        assert p.get_n'simple_name = "get_n";
        assert p.get_n = 42;
        wait;
    end process;
end architecture;
