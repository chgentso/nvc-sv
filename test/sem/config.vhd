entity sub1 is
end entity;

architecture a of sub1 is
begin
end architecture;

-------------------------------------------------------------------------------

entity sub2 is
end entity;

architecture a of sub2 is
begin
end architecture;

-------------------------------------------------------------------------------

entity top is
    attribute x : integer;
end entity;

architecture a of top is

    component comp is
    end component;

    component not_used is
    end component;

    for others : not_used use entity work.sub2(a);  -- Crash from Billowitch tc3114

begin

    c1: component comp;

end architecture;

-------------------------------------------------------------------------------

configuration conf of top is
    attribute o of 'A' : literal is 5;  -- Error
    attribute x of top : entity is 5;   -- OK
    for a
        for c1 : comp
            use entity work.sub2(a);
        end for;
        for bad : comp                  -- Error
            use entity work.sub2(a);
        end for;
    end for;
end configuration;

-------------------------------------------------------------------------------

entity gen_top is
    generic (g : integer := 3);
    port (n : integer);
end entity;

architecture a of gen_top is
    signal arr : bit_vector(1 to 2);
begin
    g1: for i in 1 to 3 generate
    end generate;
end architecture;

configuration gen_conf of gen_top is
    for a
        for g1 (1 to n)                 -- Error
        end for;
        for g1 (n)                      -- Error
        end for;
        for g1 (1 to g)                 -- OK
        end for;
        for g1 (1 to 2)                 -- OK
        end for;
        for g1 (arr'range)              -- OK
        end for;
        for g1 (2)                      -- OK
        end for;
    end for;
end configuration;
