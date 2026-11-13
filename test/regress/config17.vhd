-- Block configuration in a component configuration without a binding
-- indication for instances bound by a configuration specification
entity leaf is
    port ( o : out integer );
end entity;

architecture l1 of leaf is
begin
    o <= 1;
end architecture;

architecture l2 of leaf is
begin
    o <= 2;
end architecture;

entity mid is
    port ( o : out integer );
end entity;

architecture m1 of mid is
    component leaf is
        port ( o : out integer );
    end component;
    signal v : integer;
begin
    inner: component leaf port map (v);
    o <= 10 + v;
end architecture;

entity top17 is
end entity;

architecture test of top17 is
    component mid is
        port ( o : out integer );
    end component;

    signal r1, r2 : integer;
    for u1, u2 : mid use entity work.mid(m1);
begin
    u1: component mid port map (r1);
    u2: component mid port map (r2);

    check: process is
    begin
        wait for 1 ns;
        assert r1 = 12 report integer'image(r1);
        assert r2 = 11 report integer'image(r2);
        wait;
    end process;
end architecture;

configuration config17 of top17 is
    for test
        for u1 : mid
            for m1
                for inner : leaf
                    use entity work.leaf(l2);
                end for;
            end for;
        end for;
        for u2 : mid
            for m1
                for inner : leaf
                    use entity work.leaf(l1);
                end for;
            end for;
        end for;
    end for;
end configuration;
