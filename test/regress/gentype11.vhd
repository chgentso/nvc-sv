-- Constrained formal generic array types using 'LENGTH before instantiation
-- (IEEE 1076-2019 5.8.8, 6.5.3.2).

package gentype11_pkg is
    generic (type ix is (<>); type ca is array (ix) of integer);
    function get_len(x : ca) return natural;
end package;

package body gentype11_pkg is
    function get_len(x : ca) return natural is
    begin
        return x'length;
    end function;
end package body;

entity gentype11 is
end entity;

architecture test of gentype11 is
    subtype small is integer range 0 to 3;
    type arr4 is array (small) of integer;
    package inst is new work.gentype11_pkg generic map (ix => small, ca => arr4);
begin
    check: process is
        variable v : arr4 := (others => 1);
    begin
        assert inst.get_len(v) = 4;
        wait;
    end process;
end architecture;
