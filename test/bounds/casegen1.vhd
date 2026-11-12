entity casegen1 is
    generic ( n : integer range 0 to 3 := 1;
              m : integer := 2 );
end entity;

architecture test of casegen1 is
begin
    g1: case n generate             -- Error
        when 0 =>
        when 1 =>
        when 2 =>
    end generate;

    g2: case n generate             -- OK
        when 0 to 2 =>
        when 3 =>
    end generate;

    g3: case n generate             -- OK
        when 0 =>
        when others =>
    end generate;

    g4: case n generate             -- OK: choices not known until elaboration
        when 0 to m - 1 =>
        when others =>
    end generate;
end architecture;
