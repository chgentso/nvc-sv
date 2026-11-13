entity guard9 is
end entity;

architecture test of guard9 is
    type t_nat_vec is array (natural range <>) of natural;

    function resolved (x : t_nat_vec) return natural is
        variable r : natural;
    begin
        for i in x'range loop
            if x(i) > r then
                r := x(i);
            end if;
        end loop;
        return r;
    end function;

    subtype rnatural is resolved natural;

    signal value  : natural := 0;
    signal output : rnatural bus;
    signal output2 : rnatural bus;

    disconnect all : rnatural after 2 ns;
begin

    b1: block (value < 10) is
    begin
        with value select
            output <= guarded 42 when 3, value * 2 when others;
        output2 <= guarded value + 1;
    end block;

    check: process is
    begin
        value <= 3;
        wait for 1 ns;
        assert output = 42;
        value <= 4;
        wait for 1 ns;
        assert output = 8;
        value <= 10;
        wait for 1 ns;
        assert output = 8;
        wait for 1 ns;
        assert output = 0;
        assert output2 = 0;
        wait;
    end process;

end architecture;
