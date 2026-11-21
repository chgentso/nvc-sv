-- Incremental binding indication in a configuration declaration referencing
-- architecture signals and rebinding ports/generics (LRM 7.3.2.1).

entity config22_sub is
    generic (g : integer := 0);
    port (a : in bit := '0'; y : out integer);
end entity;

architecture rtl of config22_sub is
begin
    y <= 100 + g + 10 * bit'pos(a);
end architecture;

entity config22_tb is
end entity;

architecture arch of config22_tb is
    component sub is
        generic (g : integer := 0);
        port (a : in bit := '0'; y : out integer);
    end component;
    signal res : integer;
    signal hi  : bit := '1';
    for u1 : sub use entity work.config22_sub(rtl)
        generic map (g => 1)
        port map (a => open, y => res);
begin
    u1 : sub port map (y => res);

    check : process is
    begin
        wait for 1 ns;
        assert res = 117 report integer'image(res);
        wait;
    end process;
end architecture;

configuration config22 of config22_tb is
    for arch
        for u1 : sub
            generic map (g => 7)
            port map (a => hi);
        end for;
    end for;
end configuration;
