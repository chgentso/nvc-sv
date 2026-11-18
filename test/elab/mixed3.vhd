entity mixed3 is
end entity;

architecture test of mixed3 is
    type rec_t is record
        x : integer;
    end record;

    component mod3 is
        generic ( P : rec_t := (x => 1); N : real := 1.0 );
    end component;
begin
    u1: component mod3;                 -- Error
    u2: component mod3;                 -- No further error
end architecture;
