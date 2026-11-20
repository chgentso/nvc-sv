entity aggr2 is
end entity;

architecture test of aggr2 is
    type bv_arr_t is array (natural range <>) of bit_vector;

    signal clk : bit := '0';
    signal din, dout : bit_vector(3 downto 0) := x"0";
    signal pipe : bv_arr_t(2 downto 0)(3 downto 0) := (others => x"0");

    signal din2, dout2 : bit_vector(3 downto 0) := x"0";
    signal pipe2 : bv_arr_t(2 downto 0)(3 downto 0) := (others => x"0");
begin
    (dout, pipe) <= pipe & din when clk'event and clk = '1';
    (pipe2, dout2) <= din2 & pipe2 when clk'event and clk = '1';

    process is
        variable v_arr : bv_arr_t(1 downto 0)(3 downto 0) := (x"1", x"2");
        variable v_out : bit_vector(3 downto 0);
        variable v_in  : bit_vector(3 downto 0) := x"3";
    begin
        (v_out, v_arr) := v_arr & v_in;
        assert v_out = x"1" severity failure;
        assert v_arr(1) = x"2" and v_arr(0) = x"3" severity failure;

        (v_arr, v_out) := x"4" & v_arr;
        assert v_out = x"3" severity failure;
        assert v_arr(1) = x"4" and v_arr(0) = x"2" severity failure;

        din <= x"a";
        din2 <= x"a";
        wait for 1 ns;
        clk <= '1';
        wait for 1 ns;
        clk <= '0';
        din <= x"b";
        din2 <= x"b";
        wait for 1 ns;
        clk <= '1';
        wait for 1 ns;
        clk <= '0';
        din <= x"c";
        din2 <= x"c";
        wait for 1 ns;
        clk <= '1';
        wait for 1 ns;
        clk <= '0';
        din <= x"d";
        din2 <= x"d";
        wait for 1 ns;
        clk <= '1';
        wait for 1 ns;
        assert dout = x"a" severity failure;
        assert dout2 = x"a" severity failure;
        wait;
    end process;
end architecture;
