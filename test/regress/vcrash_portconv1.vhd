-- Unconstrained in port associated through a type conversion to a
-- constrained subtype
entity vcrash_portconv1_sub is
    port ( p : in bit_vector );
end entity;

architecture test of vcrash_portconv1_sub is
begin
    process is
    begin
        wait for 1 ns;
        assert p'left = 7 and p'right = 4;
        assert p = "1010";
        wait;
    end process;
end architecture;

-------------------------------------------------------------------------------

entity vcrash_portconv1 is
end entity;

architecture test of vcrash_portconv1 is
    subtype w4 is bit_vector(7 downto 4);
    signal s : bit_vector(0 to 3) := "1010";
begin
    u: entity work.vcrash_portconv1_sub port map ( p => w4(s) );
end architecture;
