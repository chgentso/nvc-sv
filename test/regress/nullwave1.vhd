entity nullwave1 is
end entity;

architecture test of nullwave1 is
    type bv_array is array (natural range <>) of bit;
    function resolve (x : bv_array) return bit is
    begin
        if x'length = 0 then
            return '1';
        end if;
        return x(x'left);
    end function;
    subtype rbit is resolve bit;

    signal s : rbit bus;
    signal r : rbit register;

    -- Signal parameter takes its kind from the actual
    procedure drive (signal p : inout rbit) is
    begin
        p <= '0';
        wait for 1 ns;
        p <= null;
    end procedure;
begin

    p1: process is
        procedure local is
        begin
            r <= null after 1 ns;
        end procedure;
    begin
        drive(s);
        wait for 1 ns;
        assert s = '1';
        s <= '0';
        wait for 1 ns;
        assert s = '0';
        s <= null;
        wait for 1 ns;
        assert s = '1';
        local;
        wait;
    end process;

end architecture;
