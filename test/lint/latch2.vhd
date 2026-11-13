entity latch2 is
    generic ( g_en : boolean := true );
    port ( a, b : in bit;
           q : out bit );
end entity;

architecture test of latch2 is
    constant c_off : boolean := false;
    signal s1, s2, s3, s4 : bit;
begin
    -- The branch with a static condition is always or never taken
    p1: process (all) is
    begin
        if g_en then
            s1 <= a;
        end if;
    end process;

    p2: process (all) is
    begin
        if not g_en then
            s2 <= a;
        else
            s2 <= b;
        end if;
    end process;

    p3: process (all) is
    begin
        if g_en then
            s3 <= a;
        elsif c_off then
            s3 <= b;
        end if;
    end process;

    p4: process (all) is               -- Condition is not static
    begin
        if a = '1' then
            s4 <= b;
        end if;
    end process;

    q <= s1 or s2 or s3 or s4;
end architecture;
