-- Function with a return identifier called as a subprogram actual (both
-- positional and named) and as the operand of a qualified expression
-- (IEEE 1076-2019 4.2.1).

entity func26 is
end entity;

architecture test of func26 is
    function make_idx return r of integer_vector is
        variable res : r;
    begin
        for i in r'range loop
            res(i) := i;
        end loop;
        return res;
    end function;

    procedure check_vec(v : integer_vector(7 downto 4)) is
    begin
        assert v'left = 7 and v'right = 4;
        assert v = (7, 6, 5, 4);
    end procedure;

    subtype iv3 is integer_vector(2 to 4);
begin
    main: process is
        variable q : integer_vector(2 to 4);
    begin
        check_vec(make_idx);
        check_vec(v => make_idx);
        q := iv3'(make_idx);
        assert q = (2, 3, 4);
        assert iv3'(make_idx) = (2, 3, 4);
        wait;
    end process;
end architecture;
