entity relax8 is
end entity;

architecture test of relax8 is
begin
    process is
        variable q : integer := -1;
    begin
        if q > 5 then
            q := q - 2147483648;
        end if;
        assert q = -1;
        wait;
    end process;
end architecture;
