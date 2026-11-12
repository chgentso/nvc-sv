-- FOREIGN attribute specification on a design unit must not be
-- treated as a subprogram attribute
entity vcrash_foreign1 is
end entity;

architecture impl of vcrash_foreign1 is
    attribute foreign : string;
    attribute foreign of impl : architecture is "VHPIDIRECT lib model";

    procedure p is begin end procedure;
    attribute foreign of p : procedure is "VHPIDIRECT p_impl";
begin
    process is
    begin
        report "ok";
        wait;
    end process;
end architecture;
