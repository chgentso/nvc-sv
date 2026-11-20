entity attr26 is
end entity;

architecture test of attr26 is
    type int_ptr is access integer;
    type int_file is file of integer;
    subtype bv4 is bit_vector(3 downto 0);
begin
    p : process is
        variable bv : bit_vector(0 to 1);
        variable bv2 : bv4;
        variable ptr : int_ptr;
        file f : int_file;
    begin
        assert bv'subtype'base'simple_name = "bit_vector";
        assert bv'subtype'base'path_name = ":std:standard:bit_vector";
        assert bv2'subtype'simple_name = "bv4";
        assert bv2'subtype'base'simple_name = "bit_vector";
        assert bv2'subtype'path_name = ":attr26:bv4";
        assert bv2'subtype'instance_name = ":attr26(test):bv4";
        assert ptr'subtype'simple_name = "int_ptr";
        assert ptr'subtype'base'simple_name = "int_ptr";
        assert ptr'subtype'path_name = ":attr26:int_ptr";
        assert f'subtype'simple_name = "int_file";
        assert f'subtype'base'simple_name = "int_file";
        assert f'subtype'instance_name = ":attr26(test):int_file";
        wait;
    end process;
end architecture;
