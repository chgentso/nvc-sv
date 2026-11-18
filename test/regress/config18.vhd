package config18_pack is
    constant K : integer := 5;
end package;

-------------------------------------------------------------------------------

entity leaf is
    generic (N : integer := 0);
end entity;

architecture a of leaf is
begin
    p: process is
    begin
        assert N = 5;
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity top18 is
end entity;

architecture test of top18 is
    component leaf is
        generic (N : integer := 0);
    end component;
begin
    u: component leaf;
end architecture;

-------------------------------------------------------------------------------

configuration config18 of top18 is
    for test
        use work.config18_pack.all;     -- Use clause in block configuration
        for u : leaf
            use entity work.leaf(a) generic map (N => K);
        end for;
    end for;
end configuration;
