use std.reflection.all;

entity reflect_conv1 is
end entity;

architecture beh of reflect_conv1 is
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
        variable sm : subtype_mirror;
        variable vm : value_mirror;
        variable i  : integer := 42;
    begin
        sm := integer'reflect;
        assert sm.to_integer /= null severity failure;
        assert sm.to_enumeration = null severity failure;
        assert sm.to_floating = null severity failure;
        assert sm.to_physical = null severity failure;
        assert sm.to_record = null severity failure;
        assert sm.to_array = null severity failure;
        assert sm.to_access = null severity failure;
        assert sm.to_file = null severity failure;
        assert sm.to_protected = null severity failure;

        sm := boolean'reflect;
        assert sm.to_enumeration /= null severity failure;
        assert sm.to_integer = null severity failure;

        sm := real'reflect;
        assert sm.to_floating /= null severity failure;
        assert sm.to_integer = null severity failure;

        sm := time'reflect;
        assert sm.to_physical /= null severity failure;
        assert sm.to_integer = null severity failure;

        sm := rec_t'reflect;
        assert sm.to_record /= null severity failure;
        assert sm.to_array = null severity failure;

        sm := ptr_t'reflect;
        assert sm.to_access /= null severity failure;
        assert sm.to_integer = null severity failure;

        sm := file_t'reflect;
        assert sm.to_file /= null severity failure;
        assert sm.to_integer = null severity failure;

        sm := pt_t'reflect;
        assert sm.to_protected /= null severity failure;
        assert sm.to_integer = null severity failure;

        vm := i'reflect;
        assert vm.to_integer /= null severity failure;
        assert vm.to_floating = null severity failure;
        assert vm.to_record = null severity failure;
        wait;
    end process;
end architecture;
