use std.reflection.all;

entity reflect_arrays is
end entity;

architecture test of reflect_arrays is
    subtype row is integer range 1 to 2;
    subtype col is integer range 5 downto 3;
    type grid is array (row range <>, col range <>) of integer;
begin
    process is
        variable g : grid(row, col) := ((1, 2, 3), (4, 5, 6));
        variable bv : bit_vector(7 downto 4) := "1000";
        variable sm : array_subtype_mirror;
        variable am : array_value_mirror;
    begin
        -- LOW and HIGH apply to the dimension given
        sm := g'reflect.get_subtype_mirror.to_array;
        assert sm.left(2) = 5 and sm.right(2) = 3;
        assert sm.low(2) = 3 and sm.high(2) = 5;
        assert sm.low(1) = 1 and sm.high(1) = 2;

        -- Indexes are normalised for descending ranges
        am := bv'reflect.to_array;
        assert am.get(7).to_enumeration.pos = 1;
        assert am.get(4).to_enumeration.pos = 0;

        -- Multi-dimensional index
        am := g'reflect.to_array;
        assert am.get(2, 4).to_integer.value = 5;
        assert am.get(1, 5).to_integer.value = 1;
        wait;
    end process;
end architecture;
