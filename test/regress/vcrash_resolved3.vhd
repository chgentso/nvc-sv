-- Resolved subtype of an unconstrained array whose resolution function
-- takes an array of constrained arrays
entity vcrash_resolved3 is
end entity;

architecture test of vcrash_resolved3 is
    type bv_list is array (natural range <>) of bit_vector(0 to 1);

    function rv (v : bv_list) return bit_vector is
    begin
        if v'length = 0 then
            return "00";
        end if;
        return v(v'left);
    end function;

    subtype rbv is rv bit_vector;
    signal s : rbv(0 to 1);
begin
    s <= "10";

    process is
    begin
        wait for 1 ns;
        assert s'length = 2;
        wait;
    end process;
end architecture;
