entity image_string_elem is
end entity;

architecture test of image_string_elem is
    type rec is record
        n : integer;
        s : string(1 to 2);
    end record;
begin
    process is
        variable r : rec := (3, "ab");
    begin
        assert rec'image(r) = "(3,""ab"")" report rec'image(r);
        assert string'image("xy") = """xy""" report string'image("xy");
        wait;
    end process;
end architecture;
