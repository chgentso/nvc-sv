architecture impl of lost_entity is
begin
end architecture;

entity top is
end entity;

architecture rtl of top is
    component leaf is
    end component;
begin
    u : leaf;
end architecture;

configuration cfg of top is
    for rtl
        for u : leaf
            use entity work.lost_entity(impl);
        end for;
    end for;
end configuration;
