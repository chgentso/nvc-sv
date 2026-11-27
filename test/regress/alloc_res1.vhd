entity alloc_res1 is
end entity;

architecture test of alloc_res1 is
    type int_vec is array (positive range <>) of integer;
    function res_fn(v : int_vec) return integer is
    begin
        return v(v'left);
    end function;
    type int_ptr is access integer;
begin
    process is
        variable p : int_ptr := new res_fn integer;
    begin
        wait;
    end process;
end architecture;
