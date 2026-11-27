entity opsym_assign1 is
end entity;

architecture test of opsym_assign1 is
    type rec_t is record
        v : integer;
    end record;

    function "-" (a, b : rec_t) return rec_t is
        variable v : integer := 0;
        variable r : rec_t;
    begin
        "-".v := a.v - "-".b.v;
        "-".r.v := "-".v;
        return r;
    end function;
begin
    process is
        variable x : rec_t;
    begin
        x := (v => 10) - (v => 3);
        assert x.v = 7 report "got " & integer'image(x.v);
        wait;
    end process;
end architecture;
