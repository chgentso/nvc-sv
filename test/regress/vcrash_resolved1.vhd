-- Resolution function of a constrained array subtype that is driven
-- element by element from one process and as a whole from another
entity vcrash_resolved1 is
end entity;

architecture test of vcrash_resolved1 is
    type pair is array (0 to 1) of integer;
    type pairs is array (natural range <>) of pair;

    function res (x : pairs) return pair is
        variable r : pair := (0, x'length);
    begin
        for i in x'range loop
            r(0) := r(0) + x(i)(0) + x(i)(1);
        end loop;
        return r;
    end function;

    subtype rpair is res pair;
    signal w : rpair := (0, 0);
begin
    process is
    begin
        w(0) <= 1;
        w(1) <= 2;
        wait;
    end process;

    w <= (10, 20);

    process is
    begin
        wait for 1 ns;
        assert w(0) = 33 report integer'image(w(0));
        assert w(1) = 2 report integer'image(w(1));
        wait;
    end process;
end architecture;
