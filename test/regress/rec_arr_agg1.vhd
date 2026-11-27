entity rec_arr_agg1 is
end entity;

architecture test of rec_arr_agg1 is
    type rec_t is record
        data : bit_vector;
        tag  : integer;
    end record;
    type arr_t is array (natural range <>) of rec_t;
    constant c : arr_t := (
        ("001", 1),
        ("00011", 2)
    );
begin
    process is
    begin
        report integer'image(c'length);
        wait;
    end process;
end architecture;
