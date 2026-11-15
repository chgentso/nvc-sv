entity litsep is
end entity;

architecture test of litsep is
begin
    process is
        variable y : integer;
        variable t : time;
    begin
        y := 7mod 4;                    -- Error
        y := 8 mod 4;                   -- OK
        t := 10ns;                      -- OK (unit name)
        y := 3rem 2;                    -- Error
        wait;
    end process;
end architecture;
