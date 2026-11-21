entity attr30 is
end entity;

architecture test of attr30 is
    subtype small is integer range 0 to 5;
    subtype down_t is integer range 5 downto 0;
    type color is (red, green, blue);
    subtype gb is color range green to blue;
begin
    process is
        variable v : small := 4;
        variable d : down_t := 3;
        variable lo : gb := green;
        variable hi : gb := blue;
    begin
        assert v'pos = 4;
        assert v'succ = 5;
        assert v'pred = 3;
        assert v'leftof = 3;
        assert v'rightof = 5;

        assert d'pos = 3;
        assert d'succ = 4;
        assert d'pred = 2;
        assert d'leftof = 4;
        assert d'rightof = 2;

        assert lo'pos = 1;
        assert lo'succ = blue;
        assert lo'rightof = blue;
        assert hi'pos = 2;
        assert hi'pred = green;
        assert hi'leftof = green;
        wait;
    end process;
end architecture;
