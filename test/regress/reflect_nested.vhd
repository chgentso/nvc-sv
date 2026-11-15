use std.reflection.all;

entity reflect_nested is
end entity;

architecture test of reflect_nested is
    subtype slot is integer range 0 to 1;
    type mem_base is array (slot range <>) of bit_vector(7 downto 0);
    subtype mem is mem_base(slot);
    type int_ptr is access integer;
begin
    process is
        variable m : mem := (x"00", x"ff");
        variable am : array_value_mirror;
        variable sm : array_subtype_mirror;
        variable acm : access_value_mirror;
        variable p : int_ptr := new integer'(9);
    begin
        -- Elements that are arrays
        am := m'reflect.to_array;
        assert am.get(1).to_array.get(7).to_enumeration.pos = 1;
        assert am.get(0).to_array.get(7).to_enumeration.pos = 0;
        sm := m'reflect.get_subtype_mirror.to_array;
        assert sm.element_subtype.to_array.left = 7;

        -- The designated value of an access
        acm := p'reflect.to_access;
        assert acm.get.to_integer.value = 9;
        wait;
    end process;
end architecture;
