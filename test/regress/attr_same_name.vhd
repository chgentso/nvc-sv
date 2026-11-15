package attr_same_name_pkg is
    attribute tag : integer;
end package;

-------------------------------------------------------------------------------

use work.attr_same_name_pkg.tag;

entity attr_same_name is
end entity;

architecture test of attr_same_name is
    signal s1, s2 : bit;
    attribute tag of s1 : signal is 1;
    attribute tag : string;              -- Hides the attribute from the package
    attribute tag of s2 : signal is "x";
begin
    process is
    begin
        assert s2'tag = "x";
        assert s1'tag = 1;
        wait;
    end process;
end architecture;
