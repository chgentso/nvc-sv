-- Incremental binding indication in a component configuration naming the
-- same configuration as an earlier configuration specification (LRM 7.3.2.1).

entity config19_leaf is
    generic (n : integer := 0);
    port (o : out integer);
end entity;

architecture rtl of config19_leaf is
begin
    o <= 100 + n;
end architecture;

configuration config19_c1 of config19_leaf is
    for rtl
    end for;
end configuration;

entity config19_tb is
end entity;

architecture test of config19_tb is
    component comp is
        generic (n : integer := 0);
        port (o : out integer);
    end component;

    signal x : integer;

    for u1 : comp use configuration work.config19_c1 generic map (n => 1);
begin
    u1 : comp port map (o => x);

    check : process is
    begin
        wait for 1 ns;
        assert x = 105;
        wait;
    end process;
end architecture;

configuration config19 of config19_tb is
    for test
        for u1 : comp
            use configuration work.config19_c1 generic map (n => 5);
        end for;
    end for;
end configuration;
