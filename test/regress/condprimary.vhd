entity condprimary is
end entity;

architecture test of condprimary is
    function twice (x : integer) return integer is
    begin
        return 2 * x;
    end function;
begin
    process is
        variable b : boolean := true;
        variable i, j, k : integer;
    begin
        i := 10 + (2 when b else 3);
        j := 10 + (2 when not b else 3);
        k := twice((4 when b else 5)) + (1 when not b else 7 when b else 8);
        assert i = 12;
        assert j = 13;
        assert k = 15;
        b := false;
        assert (1 when b else 9) = 9;
        wait;
    end process;
end architecture;
