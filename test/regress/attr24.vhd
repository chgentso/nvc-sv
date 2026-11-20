entity attr24_sub is
    generic (
        p : string;
        i : string
    );
end entity;

architecture rtl of attr24_sub is
begin
    assert p = ":attr24:u0:" report p severity failure;
    assert i = ":attr24(test):u0:" report i severity failure;
end architecture;

entity attr24 is
end entity;

architecture test of attr24 is
begin
    u0: entity work.attr24_sub
        generic map (
            p => u0'path_name,
            i => u0'instance_name
        );
end architecture;
