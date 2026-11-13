-- Incremental binding indications in component configurations
entity gate is
    generic ( n : integer := 0; m : integer := 100 );
    port ( i : in integer := 0; o : out integer );
end entity;

architecture a1 of gate is
begin
    o <= n + m + i;
end architecture;

architecture a2 of gate is
begin
    o <= 2 * (n + m + i);
end architecture;

entity top16 is
end entity;

architecture test of top16 is
    component gate is
        generic ( n : integer := 0 );
        port ( i : in integer := 0; o : out integer );
    end component;

    signal src : integer := 5;
    signal r1, r2, r3, r4, r5 : integer;
    for u3 : gate use entity work.gate(a2) generic map (n => 3);
    for u1, u2, u4 : gate use entity work.gate(a1);
begin
    u1: component gate generic map (n => 1) port map (src, r1);
    u2: component gate generic map (n => 2) port map (src, r2);
    u3: component gate port map (src, r3);
    u4: component gate generic map (n => 4) port map (src, r4);
    u5: component gate generic map (n => 5) port map (src, r5);  -- Default

    check: process is
    begin
        wait for 1 ns;
        assert r1 = 1 + 7 + 5 report integer'image(r1);
        assert r2 = 2 + 20 + 5 report integer'image(r2);
        assert r3 = 2 * (3 + 100 + 5) report integer'image(r3);
        assert r4 = 4 + 100 + 5 report integer'image(r4);
        -- The default architecture is the last one analysed
        assert r5 = 2 * (5 + 9 + 0) report integer'image(r5);
        wait;
    end process;
end architecture;

configuration config16 of top16 is
    for test
        -- Default binding with the generic M set by the configuration
        for u1 : gate
            generic map ( m => 7 );
        end for;
        for u2 : gate
            generic map ( m => 20 );
        end for;
        for u5 : gate
            generic map ( m => 9 ) port map ( i => open );
        end for;
    end for;
end configuration;
