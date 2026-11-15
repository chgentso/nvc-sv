entity resolution_partial is
end entity;

architecture test of resolution_partial is
    type bv_array is array (natural range <>) of bit_vector;
    subtype bv4_array is bv_array(open)(0 to 3);

    function wired_or (v : bv4_array) return bit_vector is
        variable r : bit_vector(0 to 3) := "0000";
    begin
        for i in v'range loop
            r := r or v(i);
        end loop;
        return r;
    end function;

    signal s : wired_or bit_vector(0 to 3) := "0000";
begin
    s <= "0100";
    s <= "0010";

    process is
    begin
        wait for 1 ns;
        assert s = "0110";
        wait;
    end process;
end architecture;
