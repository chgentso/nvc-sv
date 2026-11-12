-- Ports and parameters with anonymous types take the subtype of the
-- actual
entity vcrash_anontype1_r is
    port ( a : in type is private;
           b : in type is range <>;
           o : out integer );
end entity;

architecture test of vcrash_anontype1_r is
begin
    o <= integer(b) + 1 when a = a else 0;
end architecture;

-------------------------------------------------------------------------------

entity vcrash_anontype1 is
end entity;

architecture test of vcrash_anontype1 is
    signal sv : bit_vector(7 downto 0);
    signal six : integer := 6;
    signal x : integer;
begin
    u: entity work.vcrash_anontype1_r port map ( a => sv, b => six, o => x );

    process is
    begin
        wait for 1 ns;
        assert x = 7 report integer'image(x);
        wait;
    end process;
end architecture;
