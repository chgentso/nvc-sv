entity alloc_overload is
end entity;

architecture test of alloc_overload is
    type int_ptr is access integer;
    type real_ptr is access real;
    type str_ptr is access string;
    subtype nat is natural range 0 to 9;
begin
    process is
        variable ip : int_ptr;
    begin
        -- Several access types are visible for "=": the designated type
        -- of the allocator chooses the right one
        assert not (new integer'(5) = null);
        assert new real'(2.5) /= null;
        assert new string'("abc") /= null;
        assert not (new nat'(4) = null);
        ip := new integer'(3);
        assert ip /= null;
        wait;
    end process;
end architecture;
