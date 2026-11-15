entity extname_attr_sub is
end entity;

architecture test of extname_attr_sub is
begin
    process is
        variable ev, ac : boolean;
    begin
        wait on <<signal .extname_attr.s : bit>>;
        ev := <<signal .extname_attr.s : bit>>'event;
        ac := <<signal .extname_attr.s : bit>>'active;
        assert ev and ac;
        wait for 1 ns;
        ev := <<signal .extname_attr.s : bit>>'event;
        assert not ev;
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity extname_attr is
end entity;

architecture test of extname_attr is
    signal s : bit;
begin
    u: entity work.extname_attr_sub;

    process is
    begin
        wait for 1 ns;
        s <= '1';
        wait;
    end process;
end architecture;
