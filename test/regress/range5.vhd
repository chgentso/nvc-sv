-- VHDL-2019 range record expressions in subtype range constraints and loop
-- discrete ranges (LRM 5.2.1, 5.3.3.2).

entity range5 is
end entity;

architecture test of range5 is
    subtype int_rr is integer'range'record;
    constant cr : int_rr := (2, 5, ascending);
    subtype sub_s is integer range cr;
    signal vec : bit_vector(7 downto 0);
begin
    p : process is
        constant vr : natural'range'record := vec'range'value;
        variable dyn : int_rr := (4, 2, descending);
        variable sum1 : integer := 0;
        variable sum2 : integer := 0;
    begin
        for i in cr loop
            sum1 := sum1 + i;
        end loop;

        for j in dyn loop
            sum2 := sum2 * 10 + j;
        end loop;

        assert sub_s'low = 2 and sub_s'high = 5;
        assert sum1 = 14;
        assert sum2 = 432;
        assert vr.left = 7 and vr.right = 0 and vr.direction = descending;
        wait;
    end process;
end architecture;
