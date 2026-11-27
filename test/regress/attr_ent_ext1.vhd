package attr_ent_ext1_pkg is
    attribute tag : string;
end package;

use work.attr_ent_ext1_pkg.all;

entity attr_ent_ext1_sub is
    generic (g : string := "abc");
    attribute tag of attr_ent_ext1_sub : entity is g;
end entity;

architecture rtl of attr_ent_ext1_sub is
begin
    process is
    begin
        assert attr_ent_ext1_sub'tag = "abc";
        wait;
    end process;
end architecture;

entity attr_ent_ext1 is
end entity;

architecture test of attr_ent_ext1 is
begin
    assert work.attr_ent_ext1_sub'tag = "abc";
end architecture;
