entity wrap1 is
end entity;

architecture test of wrap1 is
begin
    process is
        variable q : integer := 268094693;
        variable m : integer := 16;
        variable s : integer := integer'high;
        variable r : integer := integer'low;
        variable n : integer := 1;
    begin
        q := q * m;
        assert q = -5452208;
        s := s + n;
        assert s = integer'low;
        r := r - n;
        assert r = integer'high;
        assert -r = -integer'high;
        q := -5;
        q := q - 2147483648;
        assert q = 2147483643;
        wait;
    end process;
end architecture;
