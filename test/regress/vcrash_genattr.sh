set -x

# A predefined attribute as the actual for a generic subprogram is not
# supported: this must be reported as an error, not crash

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
end architecture;
VHD

if nvc --std=08 -a genattr.vhd 2> err.txt; then exit 1; fi
cat err.txt
grep -q 'is not supported' err.txt
if grep -q 'Fatal' err.txt; then exit 1; fi
