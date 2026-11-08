entity mixed54 is
end entity;

architecture test of mixed54 is
    component sub is
        generic (FILENAME : string := "");
    end component;
begin
    u: component sub;
end architecture;
