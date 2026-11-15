entity cda_leaf is
    port (o : out integer);
end entity;

architecture l1 of cda_leaf is
begin
    o <= 1;
end architecture;

architecture l2 of cda_leaf is
begin
    o <= 2;
end architecture;

entity cda_mid is
    port (o : out integer);
end entity;

architecture s1 of cda_mid is
    component cda_leaf is
        port (o : out integer);
    end component;
    signal v : integer;
begin
    v1: cda_leaf port map (o => v);
    o <= 10 + v;
end architecture;

architecture s2 of cda_mid is
    component cda_leaf is
        port (o : out integer);
    end component;
    signal v : integer;
begin
    v1: cda_leaf port map (o => v);
    o <= 20 + v;
end architecture;

entity cda_top is
end entity;

architecture a of cda_top is
    component cda_mid is
        port (o : out integer);
    end component;
    signal x : integer;
begin
    u: cda_mid port map (o => x);

    process is
    begin
        wait for 1 ns;
        assert x = 21;
        wait;
    end process;
end architecture;

configuration config_default_arch of cda_top is
    for a
        for u : cda_mid
            -- Default binding to the architecture analysed last (S2)
            for s2
                for v1 : cda_leaf
                    use entity work.cda_leaf(l1);
                end for;
            end for;
        end for;
    end for;
end configuration;
