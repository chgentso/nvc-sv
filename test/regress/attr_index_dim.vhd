entity attr_index_dim is
end entity;

architecture test of attr_index_dim is
    type m2 is array (1 to 2, 5 downto 3) of bit;
    type um is array (natural range <>, positive range <>) of bit;
begin
    process is
        variable mm : m2;
        variable i1 : m2'index := 1;
        variable i2 : mm'index(2) := 4;
        variable u : um(0 to 1, 3 to 4);
        variable i3 : u'index(2) := 4;
    begin
        assert i2'subtype'left = 5;
        assert i2'subtype'right = 3;
        assert i1'subtype'low = 1 and i1'subtype'high = 2;
        assert i3'subtype'low = 3 and i3'subtype'high = 4;
        assert mm'index(2)'low = 3;
        assert m2'index(2)'high = 5;
        assert um'index(2)'low = positive'low;
        wait;
    end process;
end architecture;
