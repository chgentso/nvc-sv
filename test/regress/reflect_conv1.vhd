use std.reflection.all;

entity reflect_conv1 is
end entity;

architecture beh of reflect_conv1 is
    type dist_t is range 0 to 1000000 units
        mm;
        m = 1000 mm;
    end units;
    type rec_t is record
        x : integer;
        v : bit_vector(1 downto 0);
    end record;
    type ptr_t is access integer;
    type file_t is file of integer;
    type pt_t is protected
    end protected;
    type pt_t is protected body
    end protected body;
begin
    p1: process is
        variable sm  : subtype_mirror;
        variable psm : physical_subtype_mirror;
        variable pvm : physical_value_mirror;
        variable d   : dist_t := 3 m;
    begin
        sm := integer'reflect;
        assert sm.to_integer.to_subtype_mirror = sm severity failure;

        sm := boolean'reflect;
        assert sm.to_enumeration.to_subtype_mirror = sm severity failure;

        sm := real'reflect;
        assert sm.to_floating.to_subtype_mirror = sm severity failure;

        sm := dist_t'reflect;
        psm := sm.to_physical;
        assert psm.units_length = 2 severity failure;
        assert psm.unit_name(0) = "MM" severity failure;
        assert psm.unit_name(1) = "M" severity failure;
        assert psm.unit_index("mm") = 0 severity failure;
        assert psm.unit_index("m") = 1 severity failure;
        assert psm.scale(0) = 1 severity failure;
        assert psm.scale(1) = 1000 severity failure;

        pvm := d'reflect.to_physical;
        assert pvm.unit_index = 0 severity failure;
        assert pvm.value = 3000 severity failure;
        assert pvm.image = "3000 MM" severity failure;

        sm := rec_t'reflect;
        assert sm.to_record.length = 2 severity failure;

        sm := ptr_t'reflect;
        assert sm.to_access.designated_subtype.simple_name = "INTEGER" severity failure;

        sm := file_t'reflect;
        assert sm.to_file.designated_subtype.simple_name = "INTEGER" severity failure;

        sm := pt_t'reflect;
        assert sm.to_protected.to_subtype_mirror = sm severity failure;
        wait;
    end process;
end architecture;
