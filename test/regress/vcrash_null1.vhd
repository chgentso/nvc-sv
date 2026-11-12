-- Type conversion and qualified expression of null
entity vcrash_null1 is
end entity;

architecture test of vcrash_null1 is
    type int_ptr is access integer;
    type bits is array (natural range <>) of bit;
begin
    process is
        variable p, q : int_ptr;
        variable v : bit_vector(0 to 1);
    begin
        q := int_ptr(int_ptr'(null));
        assert q = null;
        p := int_ptr(int_ptr'(new integer'(5)));
        assert p.all = 5;
        v := bit_vector(bits'("01"));
        assert v = "01";
        wait;
    end process;
end architecture;
