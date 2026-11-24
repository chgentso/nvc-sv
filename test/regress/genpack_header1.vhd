package genpack_header1_pkg is
    generic (
        type t;
        c : natural
    );
    generic map (
        t => natural,
        c => 42
    );

    function get_c return natural;
    function id(x : t) return t;
end package;

package body genpack_header1_pkg is
    function get_c return natural is
        variable tmp : t;
    begin
        tmp := c;
        return tmp;
    end function;

    function id(x : t) return t is
        variable v : t := x;
    begin
        return v;
    end function;
end package body;

entity genpack_header1 is
end entity;

architecture test of genpack_header1 is
begin
    process
    begin
        assert work.genpack_header1_pkg.get_c = 42 severity failure;
        assert work.genpack_header1_pkg.id(7) = 7 severity failure;
        wait;
    end process;
end architecture;
