-- A force of mode in on an in port is seen in the port but not in the
-- signal it is associated with
entity vcrash_forcein1_sub is
    port ( i : in bit );
end entity;

architecture test of vcrash_forcein1_sub is
begin
    process is
    begin
        i <= force in '1';
        wait for 1 ns;
        assert i = '1';
        i <= release in;
        wait for 1 ns;
        assert i = '0';
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity vcrash_forcein1 is
end entity;

architecture test of vcrash_forcein1 is
    signal z : bit := '0';
begin
    u: entity work.vcrash_forcein1_sub port map ( i => z );

    process is
    begin
        wait for 500 ps;
        assert z = '0';
        wait for 1 ns;
        assert z = '0';
        wait;
    end process;
end architecture;
