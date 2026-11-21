-- Configuration specification naming an architecture analyzed later (LRM 7.3.2.2)

entity config21_sub is
    port (y : out integer);
end entity;

entity config21 is
end entity;

architecture test of config21 is
    component sub is
        port (y : out integer);
    end component;
    signal s : integer := 0;
    for u1 : sub use entity work.config21_sub(late);
begin
    u1 : sub port map (y => s);

    check : process is
    begin
        wait for 1 ns;
        assert s = 42;
        wait;
    end process;
end architecture;

architecture late of config21_sub is
begin
    y <= 42;
end architecture;
