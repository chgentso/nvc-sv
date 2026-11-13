entity nullchoice is
end entity;

architecture test of nullchoice is
    type t3 is array (1 to 3) of integer;
    type t1 is array (1 to 1) of integer;
begin
    p: process is
        variable a : t3;
        variable b : t1;
    begin
        a := (1 to 0 => 5, 1 to 3 => 1);     -- Error
        a := (1 to 3 => 1);                  -- OK
        a := (3 downto 4 => 7, others => 2); -- Error
        b := (1 to 0 => 4, 1 => 6);          -- Error
        wait;
    end process;
end architecture;
