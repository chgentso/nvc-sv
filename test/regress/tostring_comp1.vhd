entity tostring_comp1 is
end entity;

architecture beh of tostring_comp1 is
    type pair_t is record
        n : integer;
        b : bit_vector(1 downto 0);
    end record;

    type int_arr_t is array (natural range <>) of integer;
begin
    p1: process is
        variable r : pair_t := (n => 7, b => "10");
        variable a : int_arr_t(1 to 3) := (4, 5, 6);
        variable s : string(1 to 2) := "hi";
    begin
        assert to_string(r) = "(7,10)"
            report "got " & to_string(r) severity failure;
        assert to_string(a) = "(4,5,6)"
            report "got " & to_string(a) severity failure;
        assert to_string(s) = "hi"
            report "got " & to_string(s) severity failure;
        wait;
    end process;
end architecture;
