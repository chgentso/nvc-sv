-- Alias of a package using a selected name in an architecture
package vcrash_pkgalias1_pkg is
    constant c : string := ('a', 'b');
end package;

-------------------------------------------------------------------------------

entity vcrash_pkgalias1 is
end entity;

architecture test of vcrash_pkgalias1 is
    alias pkg is work.vcrash_pkgalias1_pkg;
    constant e : string := pkg.c;
begin
    process is
    begin
        assert e = "ab";
        wait;
    end process;
end architecture;
