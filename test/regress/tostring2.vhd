entity tostring2 is
end entity;

architecture test of tostring2 is
    type my_enum is (Plain, 'X', \MixedCase\, \With\\Slash\);
begin
    process is
        variable e : my_enum;
        variable c : character;
    begin
        c := CR;
        assert to_string(CR) = (1 => CR) severity failure;
        assert to_string(c) = (1 => CR) severity failure;
        assert to_string(NUL) = (1 => NUL) severity failure;
        assert to_string('A') = "A" severity failure;
        assert character'image(CR) = "cr" severity failure;

        assert to_string(Plain) = "plain" severity failure;
        assert to_string(my_enum'('X')) = "X" severity failure;
        assert to_string(\MixedCase\) = "MixedCase" severity failure;
        assert to_string(\With\\Slash\) = "With\Slash" severity failure;

        e := \With\\Slash\;
        assert to_string(e) = "With\Slash" severity failure;
        assert my_enum'image(e) = "\With\\Slash\" severity failure;
        assert to_string(false) = "false" severity failure;
        assert to_string(true) = "true" severity failure;
        wait;
    end process;
end architecture;
