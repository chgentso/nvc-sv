entity tostring_comp1 is
end entity;

architecture beh of tostring_comp1 is
    type pair_t is record
        n : integer;
        b : bit_vector(1 downto 0);
    end record;

    type int_arr_t is array (natural range <>) of integer;
    type pair_arr_t is array (natural range <>) of pair_t;
begin
    p1: process is
        variable r : pair_t := (n => 7, b => "10");
        variable a : int_arr_t(1 to 3) := (4, 5, 6);
        variable pa : pair_arr_t(1 to 2) := ((1, "01"), (2, "10"));
        variable s : string(1 to 2) := "hi";
    begin
        assert to_string(r) = "(7,10)"
            report "got " & to_string(r) severity failure;
        assert pair_t'image(r) = "(7,""10"")"
            report "got " & pair_t'image(r) severity failure;
        assert to_string(a) = "(4,5,6)"
            report "got " & to_string(a) severity failure;
        assert to_string(pa) = "((1,01),(2,10))"
            report "got " & to_string(pa) severity failure;
        assert pair_arr_t'image(pa) = "((1,""01""),(2,""10""))"
            report "got " & pair_arr_t'image(pa) severity failure;
        assert to_string(s) = "hi"
            report "got " & to_string(s) severity failure;
        wait;
    end process;
end architecture;
