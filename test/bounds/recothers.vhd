entity recothers is
end entity;

architecture test of recothers is
    type r1 is record
        f : bit_vector(3 downto 0);
        s : bit_vector(2 downto 0);
    end record;
    type r2 is record
        a : bit_vector(2 downto 0);
        b : bit_vector(2 downto 0);
        c : bit;
    end record;
    signal s1 : r1;
    signal s2 : r2;
begin
    s1 <= (s | f => "000");             -- Error
    s1 <= (f => "0000", s => "000");    -- OK
    s2 <= (a | b => "000", c => '1');   -- OK
    s2 <= (c => '1', others => "000");  -- OK
end architecture;
