set -ex

# Predefined attribute as the actual for a generic subprogram (LRM 6.5.7.2)

cat >genattr.vhd <<'VHD'
package gp is
    generic ( function img (x : integer) return string );
    function show return string;
end package;

package body gp is
    function show return string is
    begin
        return img(5);
    end function;
end package body;

entity genattr is end;

architecture a of genattr is
    package q is new work.gp generic map ( img => integer'image );
begin
    process is
    begin
        assert q.show = "5" report q.show severity failure;
        wait;
    end process;
end architecture;
VHD

nvc --std=08 -a genattr.vhd -e genattr -r genattr
