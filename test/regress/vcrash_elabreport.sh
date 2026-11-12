set -x

# A report statement in a function must not be lost because the call was
# folded while the design was elaborated

cat >elabreport.vhd <<'VHD'
package elabreport_pkg is
    function note (s : string; v : integer) return integer;
end package;

package body elabreport_pkg is
    function note (s : string; v : integer) return integer is
    begin
        report "elaborate " & s;
        return v;
    end function;
end package body;

use work.elabreport_pkg.all;

entity elabreport is end;

architecture a of elabreport is
    signal s : integer := note("t", 7);
    constant k : integer := note("k", 5);
begin
    process is
    begin
        assert s = 7 and k = 5;
        wait;
    end process;
end architecture;
VHD

nvc --std=08 -a elabreport.vhd -e elabreport 2> err.txt
cat err.txt
grep -q 'elaborate t' err.txt
grep -q 'elaborate k' err.txt
nvc --std=08 -r elabreport
