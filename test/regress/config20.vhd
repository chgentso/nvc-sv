-- Block configurations in component configurations whose instances are bound
-- without naming an architecture: by an entity aspect without architecture
-- name and by default binding (LRM 3.4.2).

entity config20_leaf is
    port (o : out integer);
end entity;

architecture l1 of config20_leaf is
begin
    o <= 1;
end architecture;

architecture l2 of config20_leaf is
begin
    o <= 2;
end architecture;

entity config20_mid is
    port (o : out integer);
end entity;

architecture s1 of config20_mid is
    component leafc is
        port (o : out integer);
    end component;
    signal v : integer;
begin
    v1 : leafc port map (o => v);
    o <= 10 + v;
end architecture;

architecture s2 of config20_mid is
    component leafc is
        port (o : out integer);
    end component;
    signal v : integer;
begin
    v1 : leafc port map (o => v);
    o <= 20 + v;
end architecture;

entity config20_tb is
end entity;

architecture test of config20_tb is
    component comp is
        port (o : out integer);
    end component;
    component config20_mid is
        port (o : out integer);
    end component;
    signal x1, x2 : integer;
begin
    u1 : comp port map (o => x1);
    u2 : config20_mid port map (o => x2);

    check : process is
    begin
        wait for 1 ns;
        assert x1 = 22;
        assert x2 = 21;
        wait;
    end process;
end architecture;

configuration config20 of config20_tb is
    for test
        for u1 : comp
            use entity work.config20_mid;
            for s2
                for v1 : leafc
                    use entity work.config20_leaf(l2);
                end for;
            end for;
        end for;
        for u2 : config20_mid
            for s2
                for v1 : leafc
                    use entity work.config20_leaf(l1);
                end for;
            end for;
        end for;
    end for;
end configuration;
