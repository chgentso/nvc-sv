package genpack_pos1_pkg is
    generic (
        type elem_t;
        size : natural;
        function cmp(a, b : elem_t) return boolean
    );
    type arr_t is array (0 to size - 1) of elem_t;
    function check_eq(a, b : elem_t) return boolean;
    function check_cmp(a, b : elem_t) return boolean;
end package;

package body genpack_pos1_pkg is
    function check_eq(a, b : elem_t) return boolean is
    begin
        return a = b;
    end function;

    function check_cmp(a, b : elem_t) return boolean is
    begin
        return cmp(a, b);
    end function;
end package body;

-------------------------------------------------------------------------------

entity genpack_pos1 is
end entity;

architecture test of genpack_pos1 is
    package p is new work.genpack_pos1_pkg generic map (natural, 4, "<");
begin
    process is
        variable v : p.arr_t;
    begin
        assert v'length = 4;
        assert p.check_eq(3, 3);
        assert not p.check_eq(3, 4);
        assert p.check_cmp(2, 5);
        assert not p.check_cmp(5, 2);
        wait;
    end process;
end architecture;
