-- LCS-2016-I14: string representation of enumeration literals that are
-- extended identifiers.  The representation keeps the case of the
-- identifier and its leading and trailing reverse solidus, and doubles
-- each reverse solidus inside it; 'VALUE accepts that representation
-- (IEEE 1076-2019 5.7, 16.2.2).

entity image3 is
end entity;

architecture test of image3 is
    type t is (\Foo\, \foo\, \a\\b\, bar, \BAR\, 'x', \\\\\\);
begin

    process is
        variable v : t;
    begin
        assert t'image(\Foo\) = "\Foo\" report t'image(\Foo\);
        assert t'image(\foo\) = "\foo\" report t'image(\foo\);
        assert t'image(\a\\b\) = "\a\\b\" report t'image(\a\\b\);
        assert t'image(bar) = "bar";
        assert t'image(\BAR\) = "\BAR\" report t'image(\BAR\);
        assert t'image('x') = "'x'";
        assert t'image(\\\\\\) = "\\\\\\" report t'image(\\\\\\);

        assert to_string(\Foo\) = "\Foo\";
        assert to_string(\a\\b\) = "\a\\b\";

        -- 'VALUE is the inverse of 'IMAGE
        for i in t'range loop
            assert t'value(t'image(i)) = i
                report "round trip failed for " & t'image(i);
        end loop;

        assert t'value("\Foo\") = \Foo\;
        assert t'value("\foo\") = \foo\;
        assert t'value("  \a\\b\ ") = \a\\b\;
        assert t'value("BAR") = bar;    -- Basic identifiers ignore case
        assert t'value("\BAR\") = \BAR\;

        v := t'value("\\\\\\");
        assert v = \\\\\\;

        wait;
    end process;

end architecture;
