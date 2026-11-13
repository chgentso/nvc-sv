entity aggothers1 is
end entity;

architecture test of aggothers1 is
    constant nbv : bit_vector(1 downto 0) := "01";

    type arg_t is array (natural range <>) of bit_vector;

    constant c1 : arg_t(1 downto 0) := (others => nbv);
    constant c2 : arg_t(0 downto 1) := (others => nbv);
    constant c3 : arg_t(3 to 5) := (others => "1010");
begin

    p1: process is
    begin
        assert c1'length = 2;
        assert c1'left = 1;
        assert c1(0) = "01" and c1(1) = "01";
        assert c1(1)'length = 2;
        assert c2'length = 0;
        assert c3'left = 3 and c3'right = 5;
        assert c3(4) = "1010";
        wait;
    end process;

end architecture;
