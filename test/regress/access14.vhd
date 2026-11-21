-- Open array constraint on an access subtype designating an unbounded array
-- of unbounded arrays, constraining only the element subtype (LRM 5.3.2.2).

entity access14 is
end entity;

architecture test of access14 is
    type bv_array is array (natural range <>) of bit_vector;
    subtype byte_array is bv_array(open)(7 downto 0);
    type bv_array_ptr is access bv_array;
    subtype nib_array_ptr is bv_array_ptr(open)(3 downto 0);
    subtype open_bits is bit_vector(open);
begin
    check: process is
        variable m : byte_array(0 to 1);
        variable p : nib_array_ptr;
        constant b : open_bits := "101";
    begin
        p := new bv_array(1 to 3)(3 downto 0);
        assert m'length = 2 and m(0)'length = 8;
        assert p.all'length = 3 and p(1)'length = 4;
        assert b'left = 0 and b'right = 2;
        deallocate(p);
        wait;
    end process;
end architecture;
