entity force5 is
end entity;

architecture test of force5 is
    signal a : bit := '0';
    signal s : bit := '0';
begin
    s <= a;

    process is
    begin
        wait for 1 ns;
        assert not s'active severity failure;
        s <= force '1';
        assert not s'active severity failure;
        assert s = '0' severity failure;
        wait for 0 ns;
        assert s'active severity failure;
        assert s'last_active = 0 ns severity failure;
        assert s = '1' severity failure;
        wait for 2 ns;
        assert not s'active severity failure;
        assert s'last_active = 2 ns severity failure;
        s <= release;
        assert not s'active severity failure;
        wait for 0 ns;
        assert s'active severity failure;
        assert s'last_active = 0 ns severity failure;
        assert s = '0' severity failure;
        wait;
    end process;
end architecture;
