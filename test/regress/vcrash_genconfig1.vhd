-- Block configurations for single iterations and ranges of a for
-- generate statement and for alternatives of if and case generate
-- statements
entity vcrash_genconfig1_e is
    port ( o : out integer );
end entity;

architecture a1 of vcrash_genconfig1_e is
begin
    o <= 1;
end architecture;

architecture a2 of vcrash_genconfig1_e is
begin
    o <= 2;
end architecture;

architecture a3 of vcrash_genconfig1_e is
begin
    o <= 3;
end architecture;

-------------------------------------------------------------------------------

entity vcrash_genconfig1_top is
    generic ( n : integer := 2;
              sel : integer := 3 );
end entity;

architecture test of vcrash_genconfig1_top is
    component comp is
        port ( o : out integer );
    end component;

    signal x : integer_vector(1 to 5);
    signal y, z, w : integer;
begin
    g: for i in 1 to 5 generate
        u: comp port map ( o => x(i) );
    end generate;

    h: if h1: n = 1 generate
        u: comp port map ( o => y );
    elsif h2: n = 2 generate
        u: comp port map ( o => y );
    else h3: generate
        u: comp port map ( o => y );
    end generate;

    k: case n generate
        when k1: 2 =>
            u: comp port map ( o => z );
        when k2: others =>
            u: comp port map ( o => z );
    end generate;

    m: if n = 2 generate
        u: comp port map ( o => w );
    end generate;

    process is
    begin
        wait for 1 ns;
        assert x = (1, 1, 3, 2, 2) report to_string(x(1)) & to_string(x(2))
            & to_string(x(3)) & to_string(x(4)) & to_string(x(5));
        assert y = 2 report integer'image(y);
        assert z = 1 report integer'image(z);
        assert w = 3 report integer'image(w);
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

configuration vcrash_genconfig1_conf of vcrash_genconfig1_top is
    for test
        for g(1 to 2)
            for u : comp
                use entity work.vcrash_genconfig1_e(a1);
            end for;
        end for;
        for g(sel)
            for u : comp
                use entity work.vcrash_genconfig1_e(a3);
            end for;
        end for;
        for g(4 to 5)
            for u : comp
                use entity work.vcrash_genconfig1_e(a2);
            end for;
        end for;
        for h(h1)
            for u : comp
                use entity work.vcrash_genconfig1_e(a1);
            end for;
        end for;
        for h(h2)
            for u : comp
                use entity work.vcrash_genconfig1_e(a2);
            end for;
        end for;
        for k(k1)
            for u : comp
                use entity work.vcrash_genconfig1_e(a1);
            end for;
        end for;
        for k(k2)
            for u : comp
                use entity work.vcrash_genconfig1_e(a2);
            end for;
        end for;
        for m
            for u : comp
                use entity work.vcrash_genconfig1_e(a3);
            end for;
        end for;
    end for;
end configuration;

-------------------------------------------------------------------------------

entity vcrash_genconfig1 is
end entity;

architecture test of vcrash_genconfig1 is
begin
    u: configuration work.vcrash_genconfig1_conf;
end architecture;
