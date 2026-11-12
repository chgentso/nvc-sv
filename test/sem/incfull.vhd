entity incfull is
end entity;

architecture test of incfull is
    type missing;                       -- Error
    type node;                          -- OK
    type ptr is access node;
    type node is record
        next_p : ptr;
    end record;
begin
    process is
        type local;                     -- Error
        type local2;                    -- OK
        type local2 is range 1 to 5;
    begin
        wait;
    end process;
end architecture;
